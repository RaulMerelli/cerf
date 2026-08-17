#pragma once

#include <cstdint>
#include <cstring>

namespace imx6_vivante {

/* Stateless Vivante GC pixel-format / ROP / DE-coordinate operations used by
   the 2D blit executor. Pure functions of their arguments (no device state).
   Format encodings + ROP3 codes: etnaviv rnndb state_2d.xml. */
struct VivanteBlitOps {
    static uint32_t Lo16(uint32_t v) { return v & 0xFFFFu; }
    static uint32_t Hi16(uint32_t v) { return (v >> 16) & 0xFFFFu; }

    static bool ResolveSourceCoordinate(uint32_t origin_field, bool relative,
                                        uint32_t destination_coordinate,
                                        uint32_t rectangle_offset,
                                        uint32_t& source_coordinate);
    static bool ResolveRopResource(bool dependency, uint32_t override_mode) {
        return override_mode == 1u ? true :
               override_mode == 2u ? false : dependency;
    }

    static uint32_t SelectSourceTransparency(bool pe20,
                                             uint32_t pe_transparency,
                                             uint32_t legacy_transparency) {
        return pe20 ? (pe_transparency & 3u)
                    : (legacy_transparency & 3u);
    }

    static bool ResolveMonoStreamCoordinate(uint32_t origin_field,
                                            bool relative,
                                            uint32_t destination_coordinate,
                                            uint32_t rectangle_offset,
                                            uint32_t& stream_coordinate);
    static bool ResolveMonoStreamExtent(uint32_t origin_field, bool relative,
                                        uint32_t rectangle_extent,
                                        uint32_t& stream_extent);
    static uint32_t StretchCoordinate(uint32_t destination_offset,
                                      uint32_t source_extent,
                                      uint32_t destination_extent,
                                      uint32_t programmed_factor,
                                      bool gdi_stretch);
    static uint32_t BppFromDeFormat(uint32_t fmt);
    static const char* DeFormatName(uint32_t fmt);

    static bool IsVrYuvFormat(uint32_t fmt) {
        switch (fmt & 0x1Fu) {
        case 7u:  /* YUY2 */
        case 8u:  /* UYVY */
        case 15u: /* YV12 */
        case 17u: /* NV12 */
        case 18u: /* NV16 */
            return true;
        default:
            return false;
        }
    }

    static uint32_t YuvToArgb(uint32_t y, uint32_t u, uint32_t v, bool bt709);

    static uint32_t Rgb565ToArgb(uint16_t px) {
        const uint32_t r5 = (px >> 11) & 0x1Fu;
        const uint32_t g6 = (px >> 5) & 0x3Fu;
        const uint32_t b5 = px & 0x1Fu;
        const uint32_t r = (r5 << 3) | (r5 >> 2);
        const uint32_t g = (g6 << 2) | (g6 >> 4);
        const uint32_t b = (b5 << 3) | (b5 >> 2);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    static uint16_t ArgbToRgb565(uint32_t argb) {
        const uint32_t r = (argb >> 16) & 0xFFu;
        const uint32_t g = (argb >> 8) & 0xFFu;
        const uint32_t b = argb & 0xFFu;
        return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    static uint32_t Rgba4444ToArgb(uint16_t px, bool alpha) {
        const uint32_t a4 = alpha ? ((px >> 12) & 0xFu) : 0xFu;
        const uint32_t r4 = (px >> 8) & 0xFu;
        const uint32_t g4 = (px >> 4) & 0xFu;
        const uint32_t b4 = px & 0xFu;
        return ((a4 * 0x11u) << 24) | ((r4 * 0x11u) << 16) |
               ((g4 * 0x11u) << 8) | (b4 * 0x11u);
    }

    static uint16_t ArgbToRgba4444(uint32_t argb, bool alpha) {
        const uint32_t a = alpha ? (((argb >> 28) & 0xFu) << 12) : 0u;
        const uint32_t r = ((argb >> 20) & 0xFu) << 8;
        const uint32_t g = ((argb >> 12) & 0xFu) << 4;
        const uint32_t b = (argb >> 4) & 0xFu;
        return static_cast<uint16_t>(a | r | g | b);
    }

    static uint32_t Rgb555ToArgb(uint16_t px, bool alpha) {
        const uint32_t a = alpha ? ((px & 0x8000u) ? 0xFF000000u : 0u) : 0xFF000000u;
        const uint32_t r5 = (px >> 10) & 0x1Fu;
        const uint32_t g5 = (px >> 5) & 0x1Fu;
        const uint32_t b5 = px & 0x1Fu;
        const uint32_t r = (r5 << 3) | (r5 >> 2);
        const uint32_t g = (g5 << 3) | (g5 >> 2);
        const uint32_t b = (b5 << 3) | (b5 >> 2);
        return a | (r << 16) | (g << 8) | b;
    }

    static uint16_t ArgbToRgb555(uint32_t argb, bool alpha) {
        const uint32_t a = alpha ? (((argb >> 31) & 1u) << 15) : 0u;
        const uint32_t r = ((argb >> 19) & 0x1Fu) << 10;
        const uint32_t g = ((argb >> 11) & 0x1Fu) << 5;
        const uint32_t b = (argb >> 3) & 0x1Fu;
        return static_cast<uint16_t>(a | r | g | b);
    }

    static uint32_t NormalizeArgb(uint32_t argb) {
        return (argb & 0xFF000000u) ? argb : (argb | 0xFF000000u);
    }

    static uint32_t Scale8(uint32_t a, uint32_t b) {
        return (a * b + 127u) / 255u;
    }

    static uint32_t AlphaOver(uint32_t src, uint32_t dst);

    struct ArgbChannels {
        uint32_t a = 0u;
        uint32_t r = 0u;
        uint32_t g = 0u;
        uint32_t b = 0u;
    };

    static ArgbChannels SplitArgb(uint32_t argb) {
        return {(argb >> 24) & 0xFFu, (argb >> 16) & 0xFFu,
                (argb >> 8) & 0xFFu, argb & 0xFFu};
    }

    static uint32_t JoinArgb(const ArgbChannels& c) {
        return ((c.a & 0xFFu) << 24) | ((c.r & 0xFFu) << 16) |
               ((c.g & 0xFFu) << 8) | (c.b & 0xFFu);
    }

    static uint32_t EffectiveAlpha(uint32_t pixel_alpha, uint32_t global_alpha,
                                   bool inverse, uint32_t global_mode) {
        uint32_t alpha = inverse ? (255u - pixel_alpha) : pixel_alpha;
        switch (global_mode & 3u) {
        case 1u: return global_alpha;
        case 2u: return Scale8(alpha, global_alpha);
        default: return alpha;
        }
    }

    static ArgbChannels BlendFactor(uint32_t mode, const ArgbChannels& reference,
                                    uint32_t source_alpha,
                                    uint32_t destination_alpha);
    static uint32_t BlendPePixel(uint32_t src_argb, uint32_t dst_argb,
                                 uint32_t alpha_control, uint32_t alpha_modes,
                                 uint32_t color_multiply_modes,
                                 uint32_t global_src_color,
                                 uint32_t global_dst_color, bool pe20);
    static bool NativeColorKeyMatch(uint32_t packed, uint32_t low,
                                    uint32_t high, uint32_t fmt);

    static size_t EndianByteOffset(size_t offset, uint32_t endian) {
        const size_t word = offset & ~static_cast<size_t>(3u);
        size_t lane = offset & 3u;
        switch (endian & 3u) {
        case 1u: lane ^= 1u; break; /* SWAP_16: A B C D -> B A D C. */
        case 2u: lane ^= 3u; break; /* SWAP_32: A B C D -> D C B A. */
        default: break;             /* NO_SWAP and reserved mode. */
        }
        return word + lane;
    }

    static uint32_t ReadPackedEndian(const uint8_t* base, size_t offset,
                                     uint32_t byte_count, uint32_t endian) {
        uint32_t packed = 0u;
        if (!base || byte_count == 0u || byte_count > 4u)
            return packed;
        for (uint32_t byte = 0u; byte < byte_count; ++byte) {
            packed |= static_cast<uint32_t>(
                base[EndianByteOffset(offset + byte, endian)]) << (byte * 8u);
        }
        return packed;
    }

    static bool ReadVrYuvPixel(const uint8_t* y_plane, uint32_t y_stride,
                               const uint8_t* u_plane, uint32_t u_stride,
                               const uint8_t* v_plane, uint32_t v_stride,
                               uint32_t x, uint32_t y, uint32_t fmt,
                               uint32_t endian, bool uv_swizzle, bool bt709,
                               uint32_t& argb);

    static void WritePackedEndian(uint8_t* base, size_t offset,
                                  uint32_t byte_count, uint32_t endian,
                                  uint32_t packed) {
        if (!base || byte_count == 0u || byte_count > 4u)
            return;
        for (uint32_t byte = 0u; byte < byte_count; ++byte) {
            base[EndianByteOffset(offset + byte, endian)] =
                static_cast<uint8_t>(packed >> (byte * 8u));
        }
    }

    static void ApplyPe10ClearBytes(uint8_t* base, size_t offset,
                                    uint32_t pixel_bytes, uint32_t low,
                                    uint32_t high, uint32_t byte_mask,
                                    uint32_t endian = 0u);

    static uint32_t ApplyReadSwizzle(uint32_t argb, uint32_t swizzle) {
        const uint32_t a = (argb >> 24) & 0xFFu;
        const uint32_t r = (argb >> 16) & 0xFFu;
        const uint32_t g = (argb >> 8) & 0xFFu;
        const uint32_t b = argb & 0xFFu;
        switch (swizzle & 3u) {
        case 1:  return (a << 24) | (g << 16) | (b << 8) | r; /* RGBA */
        case 2:  return (a << 24) | (b << 16) | (g << 8) | r; /* ABGR */
        case 3:  return (a << 24) | (b << 16) | (r << 8) | g; /* BGRA */
        default: return argb;                                  /* ARGB */
        }
    }

    static uint32_t ApplyWriteSwizzle(uint32_t argb, uint32_t swizzle) {
        const uint32_t a = (argb >> 24) & 0xFFu;
        const uint32_t r = (argb >> 16) & 0xFFu;
        const uint32_t g = (argb >> 8) & 0xFFu;
        const uint32_t b = argb & 0xFFu;
        switch (swizzle & 3u) {
        case 1:  return (a << 24) | (b << 16) | (r << 8) | g; /* inverse RGBA */
        case 2:  return (a << 24) | (b << 16) | (g << 8) | r; /* inverse ABGR */
        case 3:  return (a << 24) | (g << 16) | (b << 8) | r; /* inverse BGRA */
        default: return argb;
        }
    }

    static uint32_t UnpackSurfaceColor(uint32_t packed, uint32_t fmt,
                                       uint32_t swizzle = 0u);
    static uint32_t PackSurfaceColor(uint32_t argb, uint32_t fmt,
                                     uint32_t swizzle = 0u);

    static bool ReadSurfacePixel(const uint8_t* base, uint32_t stride,
                                 uint32_t x, uint32_t y, uint32_t fmt,
                                 uint32_t swizzle, uint32_t& argb,
                                 uint32_t endian = 0u) {
        const uint32_t bpp = BppFromDeFormat(fmt);
        if (!base || bpp == 0u) return false;
        const size_t offset = static_cast<size_t>(y) * stride +
                              static_cast<size_t>(x) * bpp;
        const uint32_t packed = ReadPackedEndian(base, offset, bpp, endian);
        argb = UnpackSurfaceColor(packed, fmt, swizzle);
        return true;
    }

    static bool WriteSurfacePixel(uint8_t* base, uint32_t stride,
                                  uint32_t x, uint32_t y, uint32_t fmt,
                                  uint32_t swizzle, uint32_t argb,
                                  uint32_t endian = 0u) {
        const uint32_t bpp = BppFromDeFormat(fmt);
        if (!base || bpp == 0u) return false;
        const uint32_t packed = PackSurfaceColor(argb, fmt, swizzle);
        const size_t offset = static_cast<size_t>(y) * stride +
                              static_cast<size_t>(x) * bpp;
        WritePackedEndian(base, offset, bpp, endian, packed);
        return true;
    }

    enum class SurfaceLayout : uint8_t {
        Linear = 0,
        Tiled,
        SuperTiled,
        MultiTiled,
        MultiSuperTiled,
        MinorTiled,
    };

    struct SurfaceLocation {
        uint32_t plane = 0u;
        size_t offset = 0u;
    };

    static const char* SurfaceLayoutName(SurfaceLayout layout) {
        switch (layout) {
        case SurfaceLayout::Linear:           return "LINEAR";
        case SurfaceLayout::Tiled:            return "TILED";
        case SurfaceLayout::SuperTiled:       return "SUPERTILED";
        case SurfaceLayout::MultiTiled:       return "MULTI_TILED";
        case SurfaceLayout::MultiSuperTiled:  return "MULTI_SUPERTILED";
        case SurfaceLayout::MinorTiled:       return "MINOR_TILED";
        default:                              return "INVALID";
        }
    }

    static bool IsMultiLayout(SurfaceLayout layout) {
        return layout == SurfaceLayout::MultiTiled ||
               layout == SurfaceLayout::MultiSuperTiled;
    }

    static uint32_t SurfaceWidthFromStride(uint32_t stride, uint32_t bpp,
                                           SurfaceLayout layout);
    static SurfaceLayout DecodeSurfaceLayout(bool tiled, bool multi_tiled,
                                             bool supertiled,
                                             bool minor_tiled);
    static SurfaceLocation LocateSurface(uint32_t stride, uint32_t x,
                                         uint32_t y, uint32_t bpp,
                                         SurfaceLayout layout,
                                         bool supertiled_new = false);

    static size_t SurfaceOffset(uint32_t stride, uint32_t x, uint32_t y,
                                uint32_t bpp, bool tiled, bool supertiled,
                                bool supertiled_new = false) {
        const SurfaceLayout layout = supertiled ? SurfaceLayout::SuperTiled
                                                : (tiled ? SurfaceLayout::Tiled
                                                         : SurfaceLayout::Linear);
        return LocateSurface(stride, x, y, bpp, layout,
                             supertiled_new).offset;
    }

    static bool ReadSurfacePackedLayout(const uint8_t* base, uint32_t stride,
                                        uint32_t x, uint32_t y, uint32_t fmt,
                                        bool tiled, bool supertiled,
                                        uint32_t& packed,
                                        bool supertiled_new = false,
                                        uint32_t endian = 0u) {
        const uint32_t bpp = BppFromDeFormat(fmt);
        if (!base || bpp == 0u) return false;
        const size_t offset = SurfaceOffset(stride, x, y, bpp, tiled,
                                            supertiled, supertiled_new);
        packed = ReadPackedEndian(base, offset, bpp, endian);
        return true;
    }

    static bool ReadSurfacePixelLayout(const uint8_t* base, uint32_t stride,
                                       uint32_t x, uint32_t y, uint32_t fmt,
                                       uint32_t swizzle, bool tiled,
                                       bool supertiled, uint32_t& argb,
                                       bool supertiled_new = false,
                                       uint32_t endian = 0u) {
        const uint32_t bpp = BppFromDeFormat(fmt);
        if (!base || bpp == 0u) return false;
        const size_t offset = SurfaceOffset(stride, x, y, bpp, tiled,
                                            supertiled, supertiled_new);
        const uint32_t packed = ReadPackedEndian(base, offset, bpp, endian);
        argb = UnpackSurfaceColor(packed, fmt, swizzle);
        return true;
    }

    static bool WriteSurfacePixelLayout(uint8_t* base, uint32_t stride,
                                        uint32_t x, uint32_t y, uint32_t fmt,
                                        uint32_t swizzle, bool tiled,
                                        bool supertiled, uint32_t argb,
                                        bool supertiled_new = false,
                                        uint32_t endian = 0u) {
        const uint32_t bpp = BppFromDeFormat(fmt);
        if (!base || bpp == 0u) return false;
        const uint32_t packed = PackSurfaceColor(argb, fmt, swizzle);
        const size_t offset = SurfaceOffset(stride, x, y, bpp, tiled,
                                            supertiled, supertiled_new);
        WritePackedEndian(base, offset, bpp, endian, packed);
        return true;
    }

    template <typename PixelFn>
    static uint32_t RasterizeLineBresenhamExclusive(uint32_t x0, uint32_t y0,
                                                     uint32_t x1, uint32_t y1,
                                                     PixelFn&& emit_pixel) {
        int32_t x = static_cast<int32_t>(x0);
        int32_t y = static_cast<int32_t>(y0);
        const int32_t end_x = static_cast<int32_t>(x1);
        const int32_t end_y = static_cast<int32_t>(y1);

        /* Etnaviv doc/2d.md: the first endpoint is rendered and the final
           endpoint is excluded.  A zero-length segment therefore emits no
           pixels, while horizontal, vertical, steep and reversed lines all
           follow the same integer Bresenham path. */
        if (x == end_x && y == end_y)
            return 0u;

        const int32_t dx = (end_x >= x) ? (end_x - x) : (x - end_x);
        const int32_t sx = (x < end_x) ? 1 : -1;
        const int32_t abs_dy = (end_y >= y) ? (end_y - y) : (y - end_y);
        const int32_t dy = -abs_dy;
        const int32_t sy = (y < end_y) ? 1 : -1;
        int32_t error = dx + dy;
        uint32_t count = 0u;

        while (x != end_x || y != end_y) {
            emit_pixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
            ++count;
            const int32_t twice_error = error * 2;
            if (twice_error >= dy) {
                error += dy;
                x += sx;
            }
            if (twice_error <= dx) {
                error += dx;
                y += sy;
            }
        }
        return count;
    }

    struct DeCoord {
        uint32_t x = 0;
        uint32_t y = 0;
    };

    static uint32_t ValidDeRot(uint32_t rot) {
        rot &= 7u;
        return (rot == 0u || rot == 1u || rot == 2u ||
                rot == 4u || rot == 5u || rot == 6u) ? rot : 0u;
    }

    static DeCoord TransformDeCoord(uint32_t x, uint32_t y,
                                    uint32_t width, uint32_t height,
                                    uint32_t rot, uint32_t mirror);

    static uint32_t RopCode(uint32_t rop, bool foreground = true) {
        const uint32_t type = (rop >> 20) & 3u;
        if (type == 3u)
            return foreground ? (rop & 0xFFu) : ((rop >> 8) & 0xFFu);
        return rop & 0xFFu;
    }

    static bool RopTruthTableDependsOn(uint32_t code, uint32_t type,
                                       uint32_t input_bit);
    static bool RopBranchUsesSource(uint32_t rop, bool foreground);
    static bool RopBranchUsesPattern(uint32_t rop, bool foreground);
    static bool RopBranchUsesDestination(uint32_t rop, bool foreground);
    static bool RopUsesSource(uint32_t rop);
    static bool RopUsesPattern(uint32_t rop);
    static bool RopUsesDestination(uint32_t rop);

    struct MonoRopSelection {
        bool write = true;
        bool foreground = true;
    };

    static MonoRopSelection SelectMonoRop(bool mono_one,
                                          bool mono_transparency_enabled,
                                          bool mono_transparent_one);

    static bool AddressRangesOverlap(uintptr_t a_begin, uintptr_t a_end,
                                     uintptr_t b_begin, uintptr_t b_end) {
        /* Half-open byte ranges. */
        return a_begin < b_end && b_begin < a_end;
    }

    static bool RopCopyLike(uint32_t rop) {
        const uint32_t type = (rop >> 20) & 3u;
        const uint32_t fg = RopCode(rop, true);
        return (type == 1u && (fg & 0xFu) == 0xCu) ||
               (type >= 2u && fg == 0xCCu);
    }

    static uint32_t ApplyRop(uint32_t rop, uint32_t dst,
                             uint32_t src, uint32_t pat,
                             bool foreground = true);
    static uint32_t ReadStreamMonoBit(const uint8_t* stream, uint32_t x,
                                      uint32_t y, uint32_t width,
                                      uint32_t pack, uint32_t stream_words);

    static bool ReadPatternBit(uint32_t low, uint32_t high,
                               uint32_t x, uint32_t y) {
        const uint32_t bit = ((y & 7u) * 8u) + (x & 7u);
        const uint32_t word = bit < 32u ? low : high;
        return ((word >> (31u - (bit & 31u))) & 1u) != 0u;
    }

    static uint32_t PatternPixel(uint32_t x, uint32_t y,
                                 uint32_t pat_cfg, uint32_t pat_low,
                                 uint32_t pat_high, uint32_t pat_bg,
                                 uint32_t pat_fg, uint32_t fallback) {
        (void)fallback;
        const bool pattern_mode = (pat_cfg & (1u << 4)) != 0u;
        const uint32_t fg = pat_fg;
        const uint32_t bg = pat_bg;
        if (!pattern_mode)
            return fg;
        const uint32_t origin_x = (pat_cfg >> 16) & 7u;
        const uint32_t origin_y = (pat_cfg >> 20) & 7u;
        return ReadPatternBit(pat_low, pat_high, x + origin_x, y + origin_y) ? fg : bg;
    }

    static void WritePixel(uint8_t* dst, uint32_t bpp, uint32_t color) {
        std::memcpy(dst, &color, bpp);
    }

    static uint32_t ReadPixel(const uint8_t* src, uint32_t bpp) {
        uint32_t v = 0;
        std::memcpy(&v, src, bpp);
        return v;
    }
};

}  // namespace imx6_vivante
