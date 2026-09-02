#pragma once

#include <cstdint>

#include "../../core/log.h"
#include "imx6_vivante_blit_ops.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_surface_access.h"

namespace imx6_vivante {

class VivanteVr : protected VivanteSurfaceAccess {
public:
    VivanteVr(VivanteState& s, VivanteMem& mem) : VivanteSurfaceAccess(mem), s_(s) {}

    void Execute(uint32_t start_value) {
        /* Video Rasterizer: one rectangle, signed 1.14 separable 9-tap
           kernels and 5-bit subpixel phases.  DE_VR_CONFIG.START is itself
           the trigger: 0=horizontal, 1=vertical, 2=one-pass. */
        constexpr uint32_t kSrcAddress = 0x01200u;
        constexpr uint32_t kSrcStride = 0x01204u;
        constexpr uint32_t kSrcRotConfig = 0x01208u;
        constexpr uint32_t kSrcConfig = 0x0120Cu;
        constexpr uint32_t kStretchX = 0x01220u;
        constexpr uint32_t kStretchY = 0x01224u;
        constexpr uint32_t kDestAddress = 0x01228u;
        constexpr uint32_t kDestStride = 0x0122Cu;
        constexpr uint32_t kDestRotConfig = 0x01230u;
        constexpr uint32_t kDestConfig = 0x01234u;
        constexpr uint32_t kAlphaControl = 0x0127Cu;
        constexpr uint32_t kAlphaModes = 0x01280u;
        constexpr uint32_t kUplaneAddress = 0x01284u;
        constexpr uint32_t kUplaneStride = 0x01288u;
        constexpr uint32_t kVplaneAddress = 0x0128Cu;
        constexpr uint32_t kVplaneStride = 0x01290u;
        constexpr uint32_t kVrSourceImageLow = 0x01298u;
        constexpr uint32_t kVrSourceImageHigh = 0x0129Cu;
        constexpr uint32_t kVrSourceOriginX = 0x012A0u;
        constexpr uint32_t kVrSourceOriginY = 0x012A4u;
        constexpr uint32_t kVrTargetLow = 0x012A8u;
        constexpr uint32_t kVrTargetHigh = 0x012ACu;
        constexpr uint32_t kDestRotHeight = 0x012B4u;
        constexpr uint32_t kSrcRotHeight = 0x012B8u;
        constexpr uint32_t kRotAngle = 0x012BCu;
        constexpr uint32_t kGlobalSrcColor = 0x012C8u;
        constexpr uint32_t kGlobalDstColor = 0x012CCu;
        constexpr uint32_t kColorMultiplyModes = 0x012D0u;
        constexpr uint32_t kPeControl = 0x012D8u;
        constexpr uint32_t kVrConfigEx = 0x012E4u;
        constexpr uint32_t kSrcExConfig = 0x01300u;
        constexpr uint32_t kSrcExAddress = 0x01304u;
        constexpr uint32_t kSharedKernel = 0x01800u;
        constexpr uint32_t kHorizontalKernel = 0x02800u;
        constexpr uint32_t kVerticalKernel = 0x02A00u;
        constexpr uint32_t kIndexColorTable32 = 0x03400u;

        const uint32_t mode = start_value & 3u;
        if (mode > 2u) {
            mem_.HaltUnsupported("imx6-vivante unsupported VR START", start_value, mode);
        }

        const uint32_t src_addr = mem_.StateReg(kSrcAddress);
        const uint32_t src_stride = mem_.StateReg(kSrcStride) & 0x3FFFFu;
        const uint32_t src_cfg = mem_.StateReg(kSrcConfig);
        const uint32_t src_ex_cfg = mem_.StateReg(kSrcExConfig);
        const uint32_t src_ex_addr = mem_.StateReg(kSrcExAddress);
        /* CERF advertises PE20; PE20 source format is SOURCE_FORMAT[28:24]. */
        const uint32_t src_fmt = (src_cfg >> 24) & 0x1Fu;
        const uint32_t src_swizzle = (src_cfg >> 20) & 3u;
        const uint32_t src_endian = (src_cfg >> 30) & 3u;
        const bool src_tiled = (src_cfg & (1u << 7)) != 0u;
        const bool src_multi_tiled = (src_ex_cfg & 1u) != 0u;
        const bool src_supertiled = (src_ex_cfg & (1u << 3)) != 0u;
        const bool src_minor_tiled = (src_ex_cfg & (1u << 8)) != 0u;
        const bool src_layout_conflict = src_minor_tiled && (src_multi_tiled || src_supertiled);
        const SurfaceLayout src_layout =
            DecodeSurfaceLayout(src_tiled, src_multi_tiled, src_supertiled, src_minor_tiled);
        const bool src_yuv = IsVrYuvFormat(src_fmt);
        const uint32_t pe_control = mem_.StateReg(kPeControl);
        const bool yuv_bt709 = (pe_control & 1u) != 0u;
        const bool uv_swizzle = (pe_control & (1u << 4)) != 0u;
        const bool yuv_to_rgb = (pe_control & (1u << 8)) != 0u;
        const bool src_format_supported = src_fmt <= 6u || src_fmt == 9u || src_fmt == 16u || src_yuv;
        const uint32_t src_bpp = src_yuv ? 0u : (src_format_supported ? BppFromDeFormat(src_fmt) : 0u);

        const uint32_t dst_addr = mem_.StateReg(kDestAddress);
        const uint32_t dst_stride = mem_.StateReg(kDestStride) & 0x3FFFFu;
        const uint32_t dst_cfg = mem_.StateReg(kDestConfig);
        const uint32_t dst_fmt = dst_cfg & 0x1Fu;
        const uint32_t dst_swizzle = (dst_cfg >> 16) & 3u;
        const uint32_t dst_endian = (dst_cfg >> 20) & 3u;
        const bool dst_tiled = (dst_cfg & (1u << 8)) != 0u;
        const bool dst_minor_tiled = (dst_cfg & (1u << 26)) != 0u;
        const SurfaceLayout dst_layout =
            dst_minor_tiled ? SurfaceLayout::MinorTiled : (dst_tiled ? SurfaceLayout::Tiled : SurfaceLayout::Linear);
        const bool dst_format_supported = dst_fmt <= 6u || dst_fmt == 16u;
        const uint32_t dst_bpp = dst_format_supported ? BppFromDeFormat(dst_fmt) : 0u;

        if (!src_format_supported || !dst_format_supported) {
            mem_.HaltUnsupported("imx6-vivante unsupported VR format", src_fmt, dst_fmt);
        }

        if (src_yuv && !yuv_to_rgb) {
            mem_.HaltUnsupported("imx6-vivante VR YUV source without PE_CONTROL.YUVRGB", src_cfg, pe_control);
        }

        /* Packed YUY2/UYVY can use every documented source layout.  Planar
           YUV has explicit U/V registers; SRC_EX_ADDRESS is already consumed
           as the second pixel-pipe surface, so split planar YUV is not a valid
           register combination and is rejected rather than guessed. */
        const bool planar_yuv = src_fmt == 15u || src_fmt == 17u || src_fmt == 18u;
        if (src_layout_conflict || (planar_yuv && IsMultiLayout(src_layout))) {
            mem_.HaltUnsupported("imx6-vivante conflicting/unsupported VR YUV layout", src_cfg, src_ex_cfg);
        }

        const bool src_base_valid =
            src_addr && src_stride && (src_bpp || src_yuv) && mem_.TranslateGpuToHost(src_addr) != nullptr &&
            (!IsMultiLayout(src_layout) || (src_ex_addr != 0u && mem_.TranslateGpuToHost(src_ex_addr) != nullptr));
        const uint32_t u_addr = mem_.StateReg(kUplaneAddress);
        const uint32_t u_stride = mem_.StateReg(kUplaneStride) & 0x3FFFFu;
        const uint32_t v_addr = mem_.StateReg(kVplaneAddress);
        const uint32_t v_stride = mem_.StateReg(kVplaneStride) & 0x3FFFFu;
        const bool u_base_valid = (u_addr && u_stride) ? mem_.TranslateGpuToHost(u_addr) != nullptr : false;
        const bool v_base_valid = (v_addr && v_stride) ? mem_.TranslateGpuToHost(v_addr) != nullptr : false;
        const bool dst_base_valid =
            dst_addr && dst_stride && dst_bpp && mem_.TranslateGpuToHostWrite(dst_addr) != nullptr;
        const bool yuv_planes_valid =
            !src_yuv || ((src_fmt == 7u || src_fmt == 8u) || ((src_fmt == 17u || src_fmt == 18u) && u_base_valid) ||
                         (src_fmt == 15u && u_base_valid && v_base_valid));
        if (!src_base_valid || !dst_base_valid || !yuv_planes_valid) {
            mem_.HaltUnsupported("imx6-vivante invalid VR surface", src_addr, dst_addr);
        }

        const uint32_t src_low = mem_.StateReg(kVrSourceImageLow);
        const uint32_t src_high = mem_.StateReg(kVrSourceImageHigh);
        const uint32_t src_left = Lo16(src_low);
        const uint32_t src_top = Hi16(src_low);
        const uint32_t src_right = Lo16(src_high);
        const uint32_t src_bottom = Hi16(src_high);
        const uint32_t target_low = mem_.StateReg(kVrTargetLow);
        const uint32_t target_high = mem_.StateReg(kVrTargetHigh);
        const uint32_t dst_left = Lo16(target_low);
        const uint32_t dst_top = Hi16(target_low);
        const uint32_t dst_right = Lo16(target_high);
        const uint32_t dst_bottom = Hi16(target_high);
        if (src_right <= src_left || src_bottom <= src_top || dst_right <= dst_left || dst_bottom <= dst_top) {
            return;
        }

        const int64_t origin_x = static_cast<int32_t>(mem_.StateReg(kVrSourceOriginX));
        const int64_t origin_y = static_cast<int32_t>(mem_.StateReg(kVrSourceOriginY));
        const uint32_t factor_x = mem_.StateReg(kStretchX) & 0x7FFFFFFFu;
        const uint32_t factor_y = mem_.StateReg(kStretchY) & 0x7FFFFFFFu;
        const uint32_t step_x = (mode == 0u || mode == 2u) ? factor_x : 0x10000u;
        const uint32_t step_y = (mode == 1u || mode == 2u) ? factor_y : 0x10000u;

        const uint32_t src_rot_cfg = mem_.StateReg(kSrcRotConfig);
        const uint32_t dst_rot_cfg = mem_.StateReg(kDestRotConfig);
        const uint32_t rot_angle = mem_.StateReg(kRotAngle);
        const uint32_t src_rot = ((rot_angle >> 8) & 1u) ? (rot_angle & 7u) : (((src_rot_cfg >> 16) & 1u) ? 4u : 0u);
        const uint32_t dst_rot =
            ((rot_angle >> 9) & 1u) ? ((rot_angle >> 3) & 7u) : (((dst_rot_cfg >> 16) & 1u) ? 4u : 0u);
        const uint32_t src_mirror = ((rot_angle >> 15) & 1u) ? ((rot_angle >> 12) & 3u) : 0u;
        const uint32_t dst_mirror = ((rot_angle >> 19) & 1u) ? ((rot_angle >> 16) & 3u) : 0u;
        const uint32_t src_layout_bpp = (src_fmt == 7u || src_fmt == 8u) ? 2u : (src_yuv ? 1u : src_bpp);
        const uint32_t src_linear_pixels_per_row = SurfaceWidthFromStride(src_stride, src_layout_bpp, src_layout);
        const uint32_t src_surface_w = (src_rot_cfg & 0xFFFFu) ? (src_rot_cfg & 0xFFFFu) : src_linear_pixels_per_row;
        const uint32_t src_surface_h =
            (mem_.StateReg(kSrcRotHeight) & 0xFFFFu) ? (mem_.StateReg(kSrcRotHeight) & 0xFFFFu) : src_bottom;
        const uint32_t dst_surface_w =
            (dst_rot_cfg & 0xFFFFu) ? (dst_rot_cfg & 0xFFFFu) : SurfaceWidthFromStride(dst_stride, dst_bpp, dst_layout);
        const uint32_t dst_surface_h =
            (mem_.StateReg(kDestRotHeight) & 0xFFFFu) ? (mem_.StateReg(kDestRotHeight) & 0xFFFFu) : dst_bottom;

        auto floorFixed16 = [](int64_t value) -> int32_t {
            if (value >= 0) return static_cast<int32_t>(value >> 16);
            return -static_cast<int32_t>(((-value) + 0xFFFFll) >> 16);
        };
        auto clampCoordinate = [](int32_t value, uint32_t low, uint32_t high_exclusive) -> uint32_t {
            if (value < static_cast<int32_t>(low)) return low;
            if (value >= static_cast<int32_t>(high_exclusive)) return high_exclusive - 1u;
            return static_cast<uint32_t>(value);
        };
        auto sourceCoord = [&](uint32_t x, uint32_t y) -> DeCoord {
            if ((ValidDeRot(src_rot) == 0u && src_mirror == 0u) || src_surface_w == 0u || src_surface_h == 0u)
                return {x, y};
            return TransformDeCoord(x, y, src_surface_w, src_surface_h, src_rot, src_mirror);
        };
        auto destCoord = [&](uint32_t x, uint32_t y) -> DeCoord {
            if ((ValidDeRot(dst_rot) == 0u && dst_mirror == 0u) || dst_surface_w == 0u || dst_surface_h == 0u)
                return {x, y};
            return TransformDeCoord(x, y, dst_surface_w, dst_surface_h, dst_rot, dst_mirror);
        };

        /* Keep A8R8G8B8 alpha intact in the VR path.  The original generic
           helper normalises alpha zero to 255, which is not the surface format
           semantics and would make filtered transparent edges opaque. */
        auto readSourcePixel = [&](int32_t x, int32_t y, uint32_t& argb) -> bool {
            const uint32_t cx = clampCoordinate(x, src_left, src_right);
            const uint32_t cy = clampCoordinate(y, src_top, src_bottom);
            const DeCoord p = sourceCoord(cx, cy);
            if (p.x >= src_surface_w || p.y >= src_surface_h) return false;

            if (src_yuv) {
                return ReadVrYuvPixelGpu(src_addr, src_ex_addr, src_stride, u_addr, u_stride, v_addr, v_stride, p.x,
                                         p.y, src_fmt, src_layout, src_endian, uv_swizzle, yuv_bt709, argb);
            }

            if ((src_fmt & 0x1Fu) == 6u || (src_fmt & 0x1Fu) == 5u) {
                uint32_t packed = 0u;
                if (!ReadLayoutPackedGpu(src_addr, src_ex_addr, src_stride, p.x, p.y, 4u, src_layout, src_endian,
                                         packed))
                    return false;
                if ((src_fmt & 0x1Fu) == 5u) packed |= 0xFF000000u;
                argb = ApplyReadSwizzle(packed, src_swizzle);
                return true;
            }
            if ((src_fmt & 0x1Fu) == 16u) {
                const uint32_t global = mem_.StateReg(kGlobalSrcColor);
                uint32_t alpha = 0u;
                if (!ReadLayoutPackedGpu(src_addr, src_ex_addr, src_stride, p.x, p.y, 1u, src_layout, src_endian,
                                         alpha))
                    return false;
                argb = ((alpha & 0xFFu) << 24) | (global & 0x00FFFFFFu);
                return true;
            }
            if ((src_fmt & 0x1Fu) == 9u) {
                uint32_t index = 0u;
                if (!ReadLayoutPackedGpu(src_addr, src_ex_addr, src_stride, p.x, p.y, 1u, src_layout, src_endian,
                                         index))
                    return false;
                argb = mem_.StateReg(kIndexColorTable32 + (index & 0xFFu) * 4u);
                return true;
            }
            return ReadSurfacePixelGpuLayout(src_addr, src_ex_addr, src_stride, p.x, p.y, src_fmt, src_swizzle,
                                             src_layout, argb, false, src_endian);
        };
        auto readDestPixel = [&](uint32_t x, uint32_t y, uint32_t& argb) -> bool {
            const DeCoord p = destCoord(x, y);
            if (p.x >= dst_surface_w || p.y >= dst_surface_h) return false;
            if ((dst_fmt & 0x1Fu) == 6u) {
                uint32_t packed = 0u;
                if (!ReadLayoutPackedGpu(dst_addr, 0u, dst_stride, p.x, p.y, 4u, dst_layout, dst_endian, packed, false,
                                         MmuClient::PixelEngine))
                    return false;
                argb = ApplyReadSwizzle(packed, dst_swizzle);
                return true;
            }
            return ReadSurfacePixelGpuLayout(dst_addr, 0u, dst_stride, p.x, p.y, dst_fmt, dst_swizzle, dst_layout, argb,
                                             false, dst_endian, MmuClient::PixelEngine);
        };
        auto writeDestPixel = [&](uint32_t x, uint32_t y, uint32_t argb) -> bool {
            const DeCoord p = destCoord(x, y);
            if (p.x >= dst_surface_w || p.y >= dst_surface_h) return false;
            if ((dst_fmt & 0x1Fu) == 6u) {
                const uint32_t packed = ApplyWriteSwizzle(argb, dst_swizzle);
                return WriteLayoutPackedGpu(dst_addr, 0u, dst_stride, p.x, p.y, 4u, dst_layout, dst_endian, packed);
            }
            return WriteSurfacePixelGpuLayout(dst_addr, 0u, dst_stride, p.x, p.y, dst_fmt, dst_swizzle, dst_layout,
                                              argb, false, dst_endian);
        };

        auto loadKernel = [&](uint32_t table_base, int64_t coordinate, int32_t& sample_base, int16_t out[9]) {
            /* The WinCE GAL generator stores 17 rows in this order:
                 row 0  = -0.5 pixel phase
                 ...
                 row 16 =  0.0 pixel phase
               The remaining positive half is obtained by mirroring taps.
               For fractions above 0.5 the sample base advances to ceil(x)
               and the non-mirrored negative phase is used. */
            sample_base = floorFixed16(coordinate);
            const uint32_t phase = (static_cast<uint32_t>(coordinate) >> 11) & 31u;
            uint32_t row = 0u;
            bool reverse_taps = false;
            if (phase <= 16u) {
                row = 16u - phase;
                reverse_taps = true;
            } else {
                ++sample_base;
                row = phase - 16u;
            }
            for (uint32_t tap = 0; tap < 9u; ++tap) {
                const uint32_t stored_tap = reverse_taps ? (8u - tap) : tap;
                const uint32_t index = row * 9u + stored_tap;
                const uint32_t packed = mem_.StateReg(table_base + (index >> 1) * 4u);
                out[tap] = static_cast<int16_t>((index & 1u) ? (packed >> 16) : (packed & 0xFFFFu));
            }
        };
        auto channel = [](uint32_t argb, uint32_t index) -> uint32_t { return (argb >> ((3u - index) * 8u)) & 0xFFu; };
        auto roundShiftSigned = [](int64_t value, uint32_t shift) -> int32_t {
            const int64_t half = 1ll << (shift - 1u);
            if (value >= 0) return static_cast<int32_t>((value + half) >> shift);
            return -static_cast<int32_t>(((-value) + half) >> shift);
        };
        auto clamp8 = [](int32_t value) -> uint32_t {
            if (value < 0) return 0u;
            if (value > 255) return 255u;
            return static_cast<uint32_t>(value);
        };
        auto packChannels = [&](const int64_t values[4], uint32_t shift) -> uint32_t {
            uint32_t argb = 0u;
            for (uint32_t c = 0; c < 4u; ++c)
                argb |= clamp8(roundShiftSigned(values[c], shift)) << ((3u - c) * 8u);
            return argb;
        };

        uint32_t active_taps = 9u;
        if (mode == 2u) {
            const uint32_t configured = (mem_.StateReg(kVrConfigEx) >> 4) & 0xFu;
            if (configured >= 1u && configured <= 9u) active_taps = configured;
        }
        const uint32_t first_tap = (9u - active_taps) / 2u;
        const uint32_t last_tap = first_tap + active_taps;
        const uint32_t horizontal_kernel = (mode == 2u) ? kHorizontalKernel : kSharedKernel;
        const uint32_t vertical_kernel = (mode == 2u) ? kVerticalKernel : kSharedKernel;
        const uint32_t alpha_control = mem_.StateReg(kAlphaControl);
        const bool alpha_enable = (alpha_control & 1u) != 0u;
        const uint32_t alpha_modes = mem_.StateReg(kAlphaModes);
        const uint32_t color_multiply_modes = mem_.StateReg(kColorMultiplyModes);
        const uint32_t global_src = mem_.StateReg(kGlobalSrcColor);
        const uint32_t global_dst = mem_.StateReg(kGlobalDstColor);

        for (uint32_t dy = dst_top; dy < dst_bottom; ++dy) {
            const int64_t fy = origin_y + static_cast<int64_t>(dy - dst_top) * step_y;
            for (uint32_t dx = dst_left; dx < dst_right; ++dx) {
                const int64_t fx = origin_x + static_cast<int64_t>(dx - dst_left) * step_x;
                uint32_t filtered = 0u;

                if (mode == 0u) { /* horizontal */
                    int16_t kx[9]{};
                    int32_t base_x = 0;
                    loadKernel(horizontal_kernel, fx, base_x, kx);
                    int64_t sums[4]{};
                    const int32_t sample_y = floorFixed16(fy);
                    for (uint32_t tx = first_tap; tx < last_tap; ++tx) {
                        uint32_t pixel = 0u;
                        if (!readSourcePixel(base_x + static_cast<int32_t>(tx) - 4, sample_y, pixel)) continue;
                        for (uint32_t c = 0; c < 4u; ++c)
                            sums[c] += static_cast<int64_t>(channel(pixel, c)) * kx[tx];
                    }
                    filtered = packChannels(sums, 14u);
                } else if (mode == 1u) { /* vertical */
                    int16_t ky[9]{};
                    int32_t base_y = 0;
                    loadKernel(vertical_kernel, fy, base_y, ky);
                    int64_t sums[4]{};
                    const int32_t sample_x = floorFixed16(fx);
                    for (uint32_t ty = first_tap; ty < last_tap; ++ty) {
                        uint32_t pixel = 0u;
                        if (!readSourcePixel(sample_x, base_y + static_cast<int32_t>(ty) - 4, pixel)) continue;
                        for (uint32_t c = 0; c < 4u; ++c)
                            sums[c] += static_cast<int64_t>(channel(pixel, c)) * ky[ty];
                    }
                    filtered = packChannels(sums, 14u);
                } else { /* one-pass: separable horizontal then vertical */
                    int16_t kx[9]{}, ky[9]{};
                    int32_t base_x = 0;
                    int32_t base_y = 0;
                    loadKernel(horizontal_kernel, fx, base_x, kx);
                    loadKernel(vertical_kernel, fy, base_y, ky);
                    int64_t sums[4]{};
                    for (uint32_t ty = first_tap; ty < last_tap; ++ty) {
                        int64_t horizontal[4]{};
                        for (uint32_t tx = first_tap; tx < last_tap; ++tx) {
                            uint32_t pixel = 0u;
                            if (!readSourcePixel(base_x + static_cast<int32_t>(tx) - 4,
                                                 base_y + static_cast<int32_t>(ty) - 4, pixel))
                                continue;
                            for (uint32_t c = 0; c < 4u; ++c)
                                horizontal[c] += static_cast<int64_t>(channel(pixel, c)) * kx[tx];
                        }
                        for (uint32_t c = 0; c < 4u; ++c)
                            sums[c] += horizontal[c] * ky[ty];
                    }
                    filtered = packChannels(sums, 28u);
                }

                if (alpha_enable) {
                    uint32_t dst_argb = 0u;
                    if (readDestPixel(dx, dy, dst_argb))
                        filtered = BlendPePixel(filtered, dst_argb, alpha_control, alpha_modes, color_multiply_modes,
                                                global_src, global_dst, true);
                }
                writeDestPixel(dx, dy, filtered);
            }
        }
    }

private:
    VivanteState& s_;
};

} // namespace imx6_vivante
