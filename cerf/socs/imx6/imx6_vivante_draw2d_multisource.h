#pragma once

#include <cstdint>

#include "imx6_vivante_draw2d_regs.h"
#include "imx6_vivante_draw2d_state.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"

namespace imx6_vivante {

class VivanteDraw2dMultiSource : protected VivanteDraw2dState {
protected:
    VivanteDraw2dMultiSource(VivanteState& s, VivanteMem& mem)
        : VivanteDraw2dState(s, mem) {}

    struct MultiSourceDesc {
        uint32_t address = 0u;
        uint32_t extra_address = 0u;
        uint32_t stride = 0u;
        uint32_t config = 0u;
        uint32_t origin = 0u;
        uint32_t size = 0u;
        uint32_t color_key_low = 0u;
        uint32_t color_key_high = 0u;
        uint32_t rop = 0u;
        uint32_t alpha_control = 0u;
        uint32_t alpha_modes = 0u;
        uint32_t global_src = 0u;
        uint32_t global_dst = 0u;
        uint32_t color_multiply = 0u;
        uint32_t transparency = 0u;
        uint32_t control = 0u;
        uint32_t ex_config = 0u;
        uint32_t rot_config = 0u;
        uint32_t rot_height = 0u;
        uint32_t rot_angle = 0u;
        uint32_t format = 0u;
        uint32_t swizzle = 0u;
        uint32_t endian = 0u;
        uint32_t bpp = 0u;
        uint32_t surface_w = 0u;
        uint32_t surface_h = 0u;
        uint32_t rotation = 0u;
        uint32_t mirror = 0u;
        bool relative = false;
        bool tiled = false;
        bool supertiled = false;
        SurfaceLayout layout = SurfaceLayout::Linear;
        bool unsupported_layout = false;
        bool valid = false;
    };

    MultiSourceDesc LoadMultiSource(uint32_t index) {
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
        m.control = mem_.StateReg(kD2dMultiSrcControl + off);
        m.ex_config = mem_.StateReg(kD2dMultiSrcExConfig + off);
        m.extra_address = mem_.StateReg(kD2dMultiSrcExAddress + off);
        m.format = (m.config >> 24) & 0x1Fu;
        m.swizzle = (m.config >> 20) & 3u;
        m.endian = (m.config >> 30) & 3u;
        m.bpp = BppFromDeFormat(m.format);
        m.relative = (m.config & (1u << 6)) != 0u;
        m.tiled = (m.config & (1u << 7)) != 0u;
        const bool multi_tiled = (m.ex_config & 1u) != 0u;
        m.supertiled = (m.ex_config & (1u << 3)) != 0u;
        const bool minor_tiled = (m.ex_config & (1u << 8)) != 0u;
        m.unsupported_layout = minor_tiled &&
            (multi_tiled || m.supertiled);
        m.layout = DecodeSurfaceLayout(m.tiled, multi_tiled,
                                       m.supertiled, minor_tiled);
        m.surface_w = (m.rot_config & 0xFFFFu)
            ? (m.rot_config & 0xFFFFu)
            : ((m.stride && m.bpp)
               ? SurfaceWidthFromStride(m.stride, m.bpp, m.layout)
               : Lo16(m.size));
        m.surface_h = (m.rot_height & 0xFFFFu)
            ? (m.rot_height & 0xFFFFu) : Hi16(m.size);
        m.rotation = ((m.rot_angle >> 8) & 1u)
            ? (m.rot_angle & 7u)
            : (((m.rot_config >> 16) & 1u) ? 4u : 0u);
        m.mirror = ((m.rot_angle >> 15) & 1u)
            ? ((m.rot_angle >> 12) & 3u) : 0u;
        if (m.address && m.stride && m.bpp && !m.unsupported_layout) {
            m.valid = mem_.TranslateGpuToHost(m.address) != nullptr &&
                (!IsMultiLayout(m.layout) ||
                 (m.extra_address != 0u &&
                  mem_.TranslateGpuToHost(m.extra_address) != nullptr));
        }
        return m;
    }

    bool ReadMultiSource(const MultiSourceDesc& m,
                        uint32_t x, uint32_t y,
                        uint32_t& argb, uint32_t& packed) {
        if (!m.valid || m.stride == 0u || m.bpp == 0u)
            return false;
        DeCoord p{x, y};
        if ((ValidDeRot(m.rotation) != 0u || m.mirror != 0u) &&
            m.surface_w != 0u && m.surface_h != 0u) {
            p = TransformDeCoord(x, y, m.surface_w, m.surface_h,
                                 m.rotation, m.mirror);
        }
        if (!ReadSurfacePackedGpuLayout(m.address, m.extra_address,
                                        m.stride, p.x, p.y, m.format,
                                        m.layout, packed, false, m.endian))
            return false;
        if (m.format == 16u) {
            argb = ((packed & 0xFFu) << 24) |
                   (m.global_src & 0x00FFFFFFu);
        } else if (m.format == 9u) {
            argb = mem_.StateReg(kD2dIndexColorTable32 +
                                 (packed & 0xFFu) * 4u);
        } else {
            argb = UnpackSurfaceColor(packed, m.format, m.swizzle);
        }
        return true;
    }

    bool ExecuteMultiSourceRect(uint32_t raw_x0, uint32_t raw_y0,
                               uint32_t x0, uint32_t y0,
                               uint32_t w, uint32_t h) {
        const uint32_t control = mem_.StateReg(kD2dMultiSource);
        const uint32_t source_count = (control & 7u) + 1u;
        if (source_count == 0u || source_count > 4u)
            return false;

        MultiSourceDesc sources[4]{};
        bool any_valid = false;
        for (uint32_t i = 0u; i < source_count; ++i) {
            sources[i] = LoadMultiSource(i);
            if (sources[i].unsupported_layout) {
                continue;
            }
            any_valid |= sources[i].valid;
        }
        if (!any_valid)
            return false;

        for (uint32_t y = 0u; y < h; ++y) {
            for (uint32_t x = 0u; x < w; ++x) {
                uint32_t current = 0u;
                uint32_t dst_packed = 0u;
                if (!ReadDstActual(x0 + x, y0 + y, current, dst_packed))
                    continue;
                if (!DestinationAccepts(dst_packed))
                    continue;

                bool changed = false;
                for (uint32_t i = 0u; i < source_count; ++i) {
                    const MultiSourceDesc& m = sources[i];
                    if (!m.valid)
                        continue;
                    const uint32_t dx = (x0 + x) - raw_x0;
                    const uint32_t dy = (y0 + y) - raw_y0;
                    uint32_t sx = 0u;
                    uint32_t sy = 0u;
                    if (!ResolveSourceCoordinate(Lo16(m.origin), m.relative,
                                                 x0 + x, dx, sx) ||
                        !ResolveSourceCoordinate(Hi16(m.origin), m.relative,
                                                 y0 + y, dy, sy))
                        continue;
                    uint32_t src_argb = 0u;
                    uint32_t src_packed = 0u;
                    if (!ReadMultiSource(m, sx, sy, src_argb, src_packed))
                        continue;
                    if ((m.transparency & 3u) == 2u &&
                        NativeColorKeyMatch(src_packed, m.color_key_low,
                                            m.color_key_high, m.format))
                        continue;

                    if ((m.alpha_control & 1u) != 0u) {
                        current = BlendPePixel(src_argb, current,
                            m.alpha_control, m.alpha_modes,
                            m.color_multiply, m.global_src, m.global_dst,
                            pe20_);
                    } else {
                        current = ApplyRop(m.rop, current, src_argb, src_argb);
                    }
                    changed = true;
                }
                if (changed)
                    WriteDst(x0 + x, y0 + y, current);
            }
        }
        return true;
    }
};

}  // namespace imx6_vivante
