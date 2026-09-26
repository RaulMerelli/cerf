#include "imx6_vivante_blit_ops.h"

namespace imx6_vivante {

uint32_t VivanteBlitFormatOps::BppFromDeFormat(uint32_t fmt) {
    switch (fmt & 0x1Fu) {
    case 0: return 2u;
    case 1: return 2u;
    case 2: return 2u;
    case 3: return 2u;
    case 4: return 2u;
    case 5: return 4u;
    case 6: return 4u;
    case 9: return 1u;
    case 16: return 1u;
    case 10: return 0u;
    default: return 0u;
    }
}

uint32_t VivanteBlitFormatOps::YuvToArgb(uint32_t y, uint32_t u, uint32_t v, bool bt709) {
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

VivanteBlitFormatOps::ArgbChannels VivanteBlitFormatOps::BlendFactor(uint32_t mode, const ArgbChannels& reference,
                                                         uint32_t source_alpha, uint32_t destination_alpha) {
    ArgbChannels f{};
    switch (mode & 7u) {
    case 0u:  return f;
    case 1u:  return {255u, 255u, 255u, 255u};
    case 2u:  return {reference.a, reference.a, reference.a, reference.a};
    case 3u:  return {255u - reference.a, 255u - reference.a, 255u - reference.a, 255u - reference.a};
    case 4u:  return reference;
    case 5u:
        return {255u - reference.a, 255u - reference.r, 255u - reference.g, 255u - reference.b};
    case 6u: {
        const uint32_t sat = source_alpha < (255u - destination_alpha) ? source_alpha : (255u - destination_alpha);
        return {255u, sat, sat, sat};
    }
    default: {
        const uint32_t sat = destination_alpha < (255u - source_alpha) ? destination_alpha : (255u - source_alpha);
        return {255u, sat, sat, sat};
    }
    }
}

uint32_t VivanteBlitFormatOps::BlendPePixel(uint32_t src_argb, uint32_t dst_argb, uint32_t alpha_control,
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
        const uint32_t global_premultiply = (color_multiply_modes >> 8) & 3u;
        if (global_premultiply == 1u) {
            src.r = Scale8(src.r, global_src.a);
            src.g = Scale8(src.g, global_src.a);
            src.b = Scale8(src.b, global_src.a);
        } else if (global_premultiply == 2u) {
            src.r = Scale8(src.r, global_src.r);
            src.g = Scale8(src.g, global_src.g);
            src.b = Scale8(src.b, global_src.b);
        }
    }

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

bool VivanteBlitFormatOps::NativeColorKeyMatch(uint32_t packed, uint32_t low, uint32_t high, uint32_t fmt) {
    struct Components {
        uint32_t value[4]{};
        uint32_t count = 0u;
    };
    auto split = [](uint32_t value, uint32_t format) -> Components {
        Components c{};
        switch (format & 0x1Fu) {
        case 0u:
            c.value[0] = (value >> 8) & 0xFu;
            c.value[1] = (value >> 4) & 0xFu;
            c.value[2] = value & 0xFu;
            c.count = 3u;
            break;
        case 1u:
            c.value[0] = (value >> 12) & 0xFu;
            c.value[1] = (value >> 8) & 0xFu;
            c.value[2] = (value >> 4) & 0xFu;
            c.value[3] = value & 0xFu;
            c.count = 4u;
            break;
        case 2u:
            c.value[0] = (value >> 10) & 0x1Fu;
            c.value[1] = (value >> 5) & 0x1Fu;
            c.value[2] = value & 0x1Fu;
            c.count = 3u;
            break;
        case 3u:
            c.value[0] = (value >> 15) & 1u;
            c.value[1] = (value >> 10) & 0x1Fu;
            c.value[2] = (value >> 5) & 0x1Fu;
            c.value[3] = value & 0x1Fu;
            c.count = 4u;
            break;
        case 4u:
            c.value[0] = (value >> 11) & 0x1Fu;
            c.value[1] = (value >> 5) & 0x3Fu;
            c.value[2] = value & 0x1Fu;
            c.count = 3u;
            break;
        case 5u:
            c.value[0] = (value >> 16) & 0xFFu;
            c.value[1] = (value >> 8) & 0xFFu;
            c.value[2] = value & 0xFFu;
            c.count = 3u;
            break;
        case 6u:
            c.value[0] = (value >> 24) & 0xFFu;
            c.value[1] = (value >> 16) & 0xFFu;
            c.value[2] = (value >> 8) & 0xFFu;
            c.value[3] = value & 0xFFu;
            c.count = 4u;
            break;
        case 9u:
        case 16u:
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

uint32_t VivanteBlitFormatOps::UnpackSurfaceColor(uint32_t packed, uint32_t fmt, uint32_t swizzle) {
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

uint32_t VivanteBlitFormatOps::PackSurfaceColor(uint32_t argb, uint32_t fmt, uint32_t swizzle) {
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

}
