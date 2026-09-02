#include "imx6_vivante_blit_ops.h"

namespace imx6_vivante {

uint32_t VivanteBlitOps::BppFromDeFormat(uint32_t fmt) {
    switch (fmt & 0x1Fu) {
    case 0: return 2u;  /* X4R4G4B4 */
    case 1: return 2u;  /* A4R4G4B4 */
    case 2: return 2u;  /* X1R5G5B5 */
    case 3: return 2u;  /* A1R5G5B5 */
    case 4: return 2u;  /* R5G6B5 */
    case 5: return 4u;  /* X8R8G8B8 */
    case 6: return 4u;  /* A8R8G8B8 */
    case 9: return 1u;  /* INDEX8, treated as byte copy */
    case 16: return 1u; /* A8 */
    case 10: return 0u; /* MONOCHROME: valid only as command-stream source */
    default: return 0u;
    }
}

const char* VivanteBlitOps::DeFormatName(uint32_t fmt) {
    switch (fmt & 0x1Fu) {
    case 0: return "X4R4G4B4";
    case 1: return "A4R4G4B4";
    case 2: return "X1R5G5B5";
    case 3: return "A1R5G5B5";
    case 4: return "R5G6B5";
    case 5: return "X8R8G8B8";
    case 6: return "A8R8G8B8";
    case 7: return "YUY2";
    case 8: return "UYVY";
    case 9: return "INDEX8";
    case 10: return "MONOCHROME";
    case 15: return "YV12";
    case 16: return "A8";
    case 17: return "NV12";
    case 18: return "NV16";
    case 19: return "RG16";
    default: return "unsupported";
    }
}

uint32_t VivanteBlitOps::YuvToArgb(uint32_t y, uint32_t u, uint32_t v, bool bt709) {
    /* Etnaviv doc/2d.md specifies limited-range BT.601/BT.709 conversion.
       Keep the integer coefficients used by the hardware documentation so
       black/white endpoints and chroma rounding match the GC320 path. */
    const int32_t a = static_cast<int32_t>(y) - 16;
    const int32_t b = static_cast<int32_t>(u) - 128;
    const int32_t c = static_cast<int32_t>(v) - 128;
    const int32_t r = bt709 ? ((298 * a + 461 * c + 128) >> 8) : ((298 * a + 410 * c + 128) >> 8);
    const int32_t g = bt709 ? ((298 * a - 55 * b - 137 * c + 128) >> 8) : ((298 * a - 101 * b - 209 * c + 128) >> 8);
    const int32_t blue = bt709 ? ((298 * a + 543 * b + 128) >> 8) : ((298 * a + 519 * b + 128) >> 8);
    auto clip = [](int32_t value) -> uint32_t {
        if (value < 0) return 0u;
        if (value > 255) return 255u;
        return static_cast<uint32_t>(value);
    };
    return 0xFF000000u | (clip(r) << 16) | (clip(g) << 8) | clip(blue);
}

uint32_t VivanteBlitOps::AlphaOver(uint32_t src, uint32_t dst) {
    /* Straight-alpha source-over.  Keep the resulting alpha instead of
       silently turning every partially transparent pixel into XRGB. */
    const uint32_t sa = (src >> 24) & 0xFFu;
    const uint32_t da = (dst >> 24) & 0xFFu;
    if (sa == 0u) return dst;
    if (sa == 255u) return src;

    const uint32_t inv = 255u - sa;
    const uint32_t out_a = sa + Scale8(da, inv);
    if (out_a == 0u) return 0u;

    auto compose = [&](uint32_t sc, uint32_t dc) -> uint32_t {
        const uint32_t premul = sc * sa + Scale8(dc * da, inv);
        const uint32_t out = (premul + out_a / 2u) / out_a;
        return out > 255u ? 255u : out;
    };
    const uint32_t r = compose((src >> 16) & 0xFFu, (dst >> 16) & 0xFFu);
    const uint32_t g = compose((src >> 8) & 0xFFu, (dst >> 8) & 0xFFu);
    const uint32_t b = compose(src & 0xFFu, dst & 0xFFu);
    return (out_a << 24) | (r << 16) | (g << 8) | b;
}

VivanteBlitOps::ArgbChannels VivanteBlitOps::BlendFactor(uint32_t mode, const ArgbChannels& reference,
                                                         uint32_t source_alpha, uint32_t destination_alpha) {
    ArgbChannels f{};
    switch (mode & 7u) {
    case 0u: /* ZERO */ return f;
    case 1u: /* ONE */ return {255u, 255u, 255u, 255u};
    case 2u: /* NORMAL */ return {reference.a, reference.a, reference.a, reference.a};
    case 3u: /* INVERSED */ return {255u - reference.a, 255u - reference.a, 255u - reference.a, 255u - reference.a};
    case 4u: /* COLOR */ return reference;
    case 5u: /* COLOR_INVERSED */
        return {255u - reference.a, 255u - reference.r, 255u - reference.g, 255u - reference.b};
    case 6u: { /* SATURATED_ALPHA */
        const uint32_t sat = source_alpha < (255u - destination_alpha) ? source_alpha : (255u - destination_alpha);
        return {255u, sat, sat, sat};
    }
    default: { /* SATURATED_DEST_ALPHA */
        const uint32_t sat = destination_alpha < (255u - source_alpha) ? destination_alpha : (255u - source_alpha);
        return {255u, sat, sat, sat};
    }
    }
}

uint32_t VivanteBlitOps::BlendPePixel(uint32_t src_argb, uint32_t dst_argb, uint32_t alpha_control,
                                      uint32_t alpha_modes, uint32_t color_multiply_modes, uint32_t global_src_color,
                                      uint32_t global_dst_color, bool pe20) {
    ArgbChannels src = SplitArgb(src_argb);
    ArgbChannels dst = SplitArgb(dst_argb);
    const ArgbChannels global_src = SplitArgb(global_src_color);
    const ArgbChannels global_dst = SplitArgb(global_dst_color);

    const uint32_t global_src_alpha = pe20 ? global_src.a : ((alpha_control >> 16) & 0xFFu);
    const uint32_t global_dst_alpha = pe20 ? global_dst.a : ((alpha_control >> 24) & 0xFFu);
    src.a = EffectiveAlpha(src.a, global_src_alpha, (alpha_modes & 1u) != 0u, (alpha_modes >> 8) & 3u);
    dst.a = EffectiveAlpha(dst.a, global_dst_alpha, (alpha_modes & (1u << 4)) != 0u, (alpha_modes >> 12) & 3u);

    const bool src_premultiply = pe20 ? ((color_multiply_modes & 1u) != 0u) : ((alpha_modes & (1u << 16)) != 0u);
    const bool dst_premultiply = pe20 ? ((color_multiply_modes & (1u << 4)) != 0u) : ((alpha_modes & (1u << 20)) != 0u);
    if (src_premultiply) {
        src.r = Scale8(src.r, src.a);
        src.g = Scale8(src.g, src.a);
        src.b = Scale8(src.b, src.a);
    }
    if (dst_premultiply) {
        dst.r = Scale8(dst.r, dst.a);
        dst.g = Scale8(dst.g, dst.a);
        dst.b = Scale8(dst.b, dst.a);
    }

    if (pe20) {
        switch ((color_multiply_modes >> 8) & 3u) {
        case 1u: /* source RGB times global source alpha */
            src.r = Scale8(src.r, global_src.a);
            src.g = Scale8(src.g, global_src.a);
            src.b = Scale8(src.b, global_src.a);
            break;
        case 2u: /* source RGB times global source color */
            src.r = Scale8(src.r, global_src.r);
            src.g = Scale8(src.g, global_src.g);
            src.b = Scale8(src.b, global_src.b);
            break;
        default: break;
        }
    }

    /* Without the FULL_DIRECTFB selector the source term is factored from
       destination and the destination term from source.  This is the form
       used by the WinCE HAL for Porter-Duff modes. */
    ArgbChannels src_reference = (alpha_modes & (1u << 27)) ? src : dst;
    ArgbChannels dst_reference = (alpha_modes & (1u << 31)) ? dst : src;
    const ArgbChannels src_factor = BlendFactor((alpha_modes >> 24) & 7u, src_reference, src.a, dst.a);
    const ArgbChannels dst_factor = BlendFactor((alpha_modes >> 28) & 7u, dst_reference, src.a, dst.a);

    auto add = [](uint32_t source, uint32_t sf, uint32_t destination, uint32_t df) -> uint32_t {
        const uint32_t value = source * sf + destination * df;
        const uint32_t rounded = (value + 127u) / 255u;
        return rounded > 255u ? 255u : rounded;
    };
    ArgbChannels out{add(src.a, src_factor.a, dst.a, dst_factor.a), add(src.r, src_factor.r, dst.r, dst_factor.r),
                     add(src.g, src_factor.g, dst.g, dst_factor.g), add(src.b, src_factor.b, dst.b, dst_factor.b)};

    /* PE20 can request a straight-alpha destination result after the
       premultiplied blend stage. */
    if (pe20 && (color_multiply_modes & (1u << 20)) != 0u && out.a != 0u) {
        auto demultiply = [&](uint32_t channel) -> uint32_t {
            const uint32_t value = (channel * 255u + out.a / 2u) / out.a;
            return value > 255u ? 255u : value;
        };
        out.r = demultiply(out.r);
        out.g = demultiply(out.g);
        out.b = demultiply(out.b);
    }
    return JoinArgb(out);
}

bool VivanteBlitOps::NativeColorKeyMatch(uint32_t packed, uint32_t low, uint32_t high, uint32_t fmt) {
    struct Components {
        uint32_t value[4]{};
        uint32_t count = 0u;
    };
    auto split = [](uint32_t value, uint32_t format) -> Components {
        Components c{};
        switch (format & 0x1Fu) {
        case 0u: /* X4R4G4B4 */
            c.value[0] = (value >> 8) & 0xFu;
            c.value[1] = (value >> 4) & 0xFu;
            c.value[2] = value & 0xFu;
            c.count = 3u;
            break;
        case 1u: /* A4R4G4B4 */
            c.value[0] = (value >> 12) & 0xFu;
            c.value[1] = (value >> 8) & 0xFu;
            c.value[2] = (value >> 4) & 0xFu;
            c.value[3] = value & 0xFu;
            c.count = 4u;
            break;
        case 2u: /* X1R5G5B5 */
            c.value[0] = (value >> 10) & 0x1Fu;
            c.value[1] = (value >> 5) & 0x1Fu;
            c.value[2] = value & 0x1Fu;
            c.count = 3u;
            break;
        case 3u: /* A1R5G5B5 */
            c.value[0] = (value >> 15) & 1u;
            c.value[1] = (value >> 10) & 0x1Fu;
            c.value[2] = (value >> 5) & 0x1Fu;
            c.value[3] = value & 0x1Fu;
            c.count = 4u;
            break;
        case 4u: /* R5G6B5 */
            c.value[0] = (value >> 11) & 0x1Fu;
            c.value[1] = (value >> 5) & 0x3Fu;
            c.value[2] = value & 0x1Fu;
            c.count = 3u;
            break;
        case 5u: /* X8R8G8B8 */
            c.value[0] = (value >> 16) & 0xFFu;
            c.value[1] = (value >> 8) & 0xFFu;
            c.value[2] = value & 0xFFu;
            c.count = 3u;
            break;
        case 6u: /* A8R8G8B8 */
            c.value[0] = (value >> 24) & 0xFFu;
            c.value[1] = (value >> 16) & 0xFFu;
            c.value[2] = (value >> 8) & 0xFFu;
            c.value[3] = value & 0xFFu;
            c.count = 4u;
            break;
        case 9u:  /* INDEX8 */
        case 16u: /* A8 */
            c.value[0] = value & 0xFFu;
            c.count = 1u;
            break;
        default:
            c.value[0] = value;
            c.count = 1u;
            break;
        }
        return c;
    };

    const Components pixel = split(packed, fmt);
    const Components lo = split(low, fmt);
    const Components hi = split(high, fmt);
    for (uint32_t i = 0u; i < pixel.count; ++i) {
        if (pixel.value[i] < lo.value[i] || pixel.value[i] > hi.value[i]) return false;
    }
    return true;
}

bool VivanteBlitOps::ReadVrYuvPixel(const uint8_t* y_plane, uint32_t y_stride, const uint8_t* u_plane,
                                    uint32_t u_stride, const uint8_t* v_plane, uint32_t v_stride, uint32_t x,
                                    uint32_t y, uint32_t fmt, uint32_t endian, bool uv_swizzle, bool bt709,
                                    uint32_t& argb) {
    if (!y_plane || y_stride == 0u) return false;

    uint32_t yy = 0u;
    uint32_t uu = 128u;
    uint32_t vv = 128u;
    switch (fmt & 0x1Fu) {
    case 7u: { /* YUY2: Y0 U Y1 V */
        const size_t pair = static_cast<size_t>(y) * y_stride + static_cast<size_t>(x >> 1) * 4u;
        const uint32_t packed = ReadPackedEndian(y_plane, pair, 4u, endian);
        yy = (packed >> ((x & 1u) ? 16u : 0u)) & 0xFFu;
        uu = (packed >> 8) & 0xFFu;
        vv = (packed >> 24) & 0xFFu;
        break;
    }
    case 8u: { /* UYVY: U Y0 V Y1 */
        const size_t pair = static_cast<size_t>(y) * y_stride + static_cast<size_t>(x >> 1) * 4u;
        const uint32_t packed = ReadPackedEndian(y_plane, pair, 4u, endian);
        yy = (packed >> ((x & 1u) ? 24u : 8u)) & 0xFFu;
        uu = packed & 0xFFu;
        vv = (packed >> 16) & 0xFFu;
        break;
    }
    case 15u: { /* YV12 uses explicit U/V plane registers. */
        if (!u_plane || !v_plane || u_stride == 0u || v_stride == 0u) return false;
        yy = y_plane[EndianByteOffset(static_cast<size_t>(y) * y_stride + x, endian)];
        const size_t ux = x >> 1;
        const size_t uy = y >> 1;
        uu = u_plane[EndianByteOffset(uy * u_stride + ux, endian)];
        vv = v_plane[EndianByteOffset(uy * v_stride + ux, endian)];
        break;
    }
    case 17u:   /* NV12: 2x2 chroma subsampling. */
    case 18u: { /* NV16: 2x1 chroma subsampling. */
        if (!u_plane || u_stride == 0u) return false;
        yy = y_plane[EndianByteOffset(static_cast<size_t>(y) * y_stride + x, endian)];
        const size_t chroma_y = ((fmt & 0x1Fu) == 17u) ? (y >> 1) : y;
        const size_t uv = chroma_y * u_stride + (x & ~1u);
        uu = u_plane[EndianByteOffset(uv, endian)];
        vv = u_plane[EndianByteOffset(uv + 1u, endian)];
        break;
    }
    default: return false;
    }

    if (uv_swizzle) {
        const uint32_t tmp = uu;
        uu = vv;
        vv = tmp;
    }
    argb = YuvToArgb(yy, uu, vv, bt709);
    return true;
}

void VivanteBlitOps::ApplyPe10ClearBytes(uint8_t* base, size_t offset, uint32_t pixel_bytes, uint32_t low,
                                         uint32_t high, uint32_t byte_mask, uint32_t endian) {
    if (!base || pixel_bytes == 0u) return;
    const uint64_t pattern = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
    for (uint32_t byte = 0u; byte < pixel_bytes; ++byte) {
        const uint32_t slot = static_cast<uint32_t>((offset + byte) & 7u);
        if ((byte_mask & (1u << slot)) == 0u) continue;
        base[EndianByteOffset(offset + byte, endian)] = static_cast<uint8_t>(pattern >> (slot * 8u));
    }
}

uint32_t VivanteBlitOps::UnpackSurfaceColor(uint32_t packed, uint32_t fmt, uint32_t swizzle) {
    switch (fmt & 0x1Fu) {
    case 0: return Rgba4444ToArgb(static_cast<uint16_t>(packed), false);
    case 1: return Rgba4444ToArgb(static_cast<uint16_t>(packed), true);
    case 2: return Rgb555ToArgb(static_cast<uint16_t>(packed), false);
    case 3: return Rgb555ToArgb(static_cast<uint16_t>(packed), true);
    case 4: return Rgb565ToArgb(static_cast<uint16_t>(packed));
    case 5: return ApplyReadSwizzle(packed | 0xFF000000u, swizzle);
    case 6: return ApplyReadSwizzle(packed, swizzle);
    case 9: return 0xFF000000u | ((packed & 0xFFu) * 0x010101u);
    case 16: return ((packed & 0xFFu) << 24) | 0x00FFFFFFu;
    default: return NormalizeArgb(packed);
    }
}

uint32_t VivanteBlitOps::PackSurfaceColor(uint32_t argb, uint32_t fmt, uint32_t swizzle) {
    switch (fmt & 0x1Fu) {
    case 0: return ArgbToRgba4444(argb, false);
    case 1: return ArgbToRgba4444(argb, true);
    case 2: return ArgbToRgb555(argb, false);
    case 3: return ArgbToRgb555(argb, true);
    case 4: return ArgbToRgb565(argb);
    case 5: return ApplyWriteSwizzle(argb | 0xFF000000u, swizzle);
    case 6: return ApplyWriteSwizzle(argb, swizzle);
    case 9:
    case 16: return (argb >> 24) & 0xFFu;
    default: return argb;
    }
}

} // namespace imx6_vivante
