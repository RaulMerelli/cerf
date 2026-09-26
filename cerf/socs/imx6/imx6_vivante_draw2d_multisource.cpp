#include "imx6_vivante_draw2d_multisource.h"

#include "imx6_vivante_draw2d_regs.h"

namespace imx6_vivante {

VivanteDraw2dMultiSource::MultiSourceDesc VivanteDraw2dMultiSource::LoadMultiSource(uint32_t index) {
    MultiSourceDesc m{};
    const uint32_t off = index * 4u;
    m.address = mem_.StateReg(kD2dMultiSrcAddress + off);
    m.stride = mem_.StateReg(kD2dMultiSrcStride + off) & 0x3FFFFu;
    m.rot_config = mem_.StateReg(kD2dMultiSrcRotConfig + off);
    m.config = mem_.StateReg(kD2dMultiSrcConfig + off);
    m.origin = mem_.StateReg(kD2dMultiSrcOrigin + off);
    m.size = mem_.StateReg(kD2dMultiSrcSize + off);
    m.color_key_low = mem_.StateReg(kD2dMultiSrcColorBg + off);
    m.color_key_high = mem_.StateReg(kD2dMultiSrcColorKeyHigh + off);
    m.rop = mem_.StateReg(kD2dMultiSrcRop + off);
    m.alpha_control = mem_.StateReg(kD2dMultiSrcAlphaControl + off);
    m.alpha_modes = mem_.StateReg(kD2dMultiSrcAlphaModes + off);
    m.rot_height = mem_.StateReg(kD2dMultiSrcRotHeight + off);
    m.rot_angle = mem_.StateReg(kD2dMultiSrcRotAngle + off);
    m.global_src = mem_.StateReg(kD2dMultiSrcGlobalSrcColor + off);
    m.global_dst = mem_.StateReg(kD2dMultiSrcGlobalDstColor + off);
    m.color_multiply = mem_.StateReg(kD2dMultiSrcColorMultiply + off);
    m.transparency = mem_.StateReg(kD2dMultiSrcTransparency + off);
    if (!IsValidPeTransparency(m.transparency))
        mem_.HaltUnsupported("imx6-vivante multi-source PE_TRANSPARENCY encoding is not assigned",
                             m.transparency, 0);
    if (!IsValidColorMultiplyModes(m.color_multiply))
        mem_.HaltUnsupported("imx6-vivante multi-source global premultiply encoding is not assigned",
                             m.color_multiply, 0);
    m.control = mem_.StateReg(kD2dMultiSrcControl + off);
    m.ex_config = mem_.StateReg(kD2dMultiSrcExConfig + off);
    m.extra_address = mem_.StateReg(kD2dMultiSrcExAddress + off);
    m.format = (m.config >> 24) & 0x1Fu;
    m.swizzle = (m.config >> 20) & 3u;
    m.endian = (m.config >> 30) & 3u;
    if (!IsValidEndian(m.endian))
        mem_.HaltUnsupported("imx6-vivante multi-source ENDIAN_MODE encoding is not assigned", m.config, m.endian);
    if (!IsValidGlobalAlphaModes(m.alpha_modes))
        mem_.HaltUnsupported("imx6-vivante multi-source global alpha mode encoding is not assigned", m.alpha_modes, 0);
    m.bpp = RequireMemoryDeFormat(m.format, "imx6-vivante unsupported multi-source format");
    m.relative = (m.config & (1u << 6)) != 0u;
    m.tiled = (m.config & (1u << 7)) != 0u;
    const bool multi_tiled = (m.ex_config & 1u) != 0u;
    m.supertiled = (m.ex_config & (1u << 3)) != 0u;
    const bool minor_tiled = (m.ex_config & (1u << 8)) != 0u;
    m.unsupported_layout = minor_tiled && (multi_tiled || m.supertiled);
    m.layout = DecodeSurfaceLayout(m.tiled, multi_tiled, m.supertiled, minor_tiled);
    m.surface_w = (m.rot_config & 0xFFFFu)
                      ? (m.rot_config & 0xFFFFu)
                      : ((m.stride && m.bpp) ? SurfaceWidthFromStride(m.stride, m.bpp, m.layout) : Lo16(m.size));
    m.surface_h = (m.rot_height & 0xFFFFu) ? (m.rot_height & 0xFFFFu) : Hi16(m.size);
    m.rotation = ((m.rot_angle >> 8) & 1u) ? (m.rot_angle & 7u) : (((m.rot_config >> 16) & 1u) ? 4u : 0u);
    m.mirror = ((m.rot_angle >> 15) & 1u) ? ((m.rot_angle >> 12) & 3u) : 0u;
    if (!IsValidDeRot(m.rotation))
        mem_.HaltUnsupported("imx6-vivante multi-source DE_ROT_MODE encoding is not assigned", m.rot_angle, m.rotation);
    if (m.address && m.stride && m.bpp && !m.unsupported_layout) {
        m.valid = mem_.TranslateGpuToHost(m.address) != nullptr &&
                  (!IsMultiLayout(m.layout) ||
                   (m.extra_address != 0u && mem_.TranslateGpuToHost(m.extra_address) != nullptr));
    }
    return m;
}

void VivanteDraw2dMultiSource::ReadMultiSource(const MultiSourceDesc& m, uint32_t x, uint32_t y, uint32_t& argb,
                                               uint32_t& packed) {
    DeCoord p{x, y};
    if ((m.rotation != 0u || m.mirror != 0u) && m.surface_w != 0u && m.surface_h != 0u) {
        p = TransformDeCoord(x, y, m.surface_w, m.surface_h, m.rotation, m.mirror);
    }
    if (!ReadSurfacePackedGpuLayout(m.address, m.extra_address, m.stride, p.x, p.y, m.format, m.layout, packed, false,
                                    m.endian))
        mem_.HaltUnsupported("imx6-vivante multi-source pixel read outside GPU-mapped memory", m.address,
                             (static_cast<uint64_t>(p.y) << 32) | p.x);
    if (m.format == 16u) {
        argb = ((packed & 0xFFu) << 24) | (m.global_src & 0x00FFFFFFu);
    } else if (m.format == 9u) {
        argb = mem_.StateReg(kD2dIndexColorTable32 + (packed & 0xFFu) * 4u);
    } else {
        argb = UnpackSurfaceColor(packed, m.format, m.swizzle);
    }
}

bool VivanteDraw2dMultiSource::ExecuteMultiSourceRect(uint32_t raw_x0, uint32_t raw_y0, uint32_t x0, uint32_t y0,
                                                      uint32_t w, uint32_t h) {
    const uint32_t control = mem_.StateReg(kD2dMultiSource);
    const uint32_t source_count = (control & 7u) + 1u;
    if (source_count == 0u || source_count > 4u) return false;

    MultiSourceDesc sources[4]{};
    bool any_valid = false;
    for (uint32_t i = 0u; i < source_count; ++i) {
        sources[i] = LoadMultiSource(i);
        if (sources[i].unsupported_layout) {
            continue;
        }
        any_valid |= sources[i].valid;
    }
    if (!any_valid) return false;

    for (uint32_t y = 0u; y < h; ++y) {
        for (uint32_t x = 0u; x < w; ++x) {
            uint32_t current = 0u;
            uint32_t dst_packed = 0u;
            ReadDstActual(x0 + x, y0 + y, current, dst_packed);
            if (!DestinationAccepts(dst_packed)) continue;

            bool changed = false;
            for (uint32_t i = 0u; i < source_count; ++i) {
                const MultiSourceDesc& m = sources[i];
                if (!m.valid) continue;
                const uint32_t dx = (x0 + x) - raw_x0;
                const uint32_t dy = (y0 + y) - raw_y0;
                uint32_t sx = 0u;
                uint32_t sy = 0u;
                if (!ResolveSourceCoordinate(Lo16(m.origin), m.relative, x0 + x, dx, sx) ||
                    !ResolveSourceCoordinate(Hi16(m.origin), m.relative, y0 + y, dy, sy))
                    continue;
                uint32_t src_argb = 0u;
                uint32_t src_packed = 0u;
                ReadMultiSource(m, sx, sy, src_argb, src_packed);
                if ((m.transparency & 3u) == 2u &&
                    NativeColorKeyMatch(src_packed, m.color_key_low, m.color_key_high, m.format))
                    continue;

                if ((m.alpha_control & 1u) != 0u) {
                    current = BlendPePixel(src_argb, current, m.alpha_control, m.alpha_modes, m.color_multiply,
                                           m.global_src, m.global_dst, pe20_);
                } else {
                    current = ApplyRop(m.rop, current, src_argb, src_argb);
                }
                changed = true;
            }
            if (changed) WriteDst(x0 + x, y0 + y, current);
        }
    }
    return true;
}

}
