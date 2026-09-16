#include "imx6_vivante_blit_ops.h"

namespace imx6_vivante {

bool VivanteBlitRopOps::RopTruthTableDependsOn(uint32_t code, uint32_t type, uint32_t input_bit) {
    const uint32_t inputs = type == 0u || type == 1u ? 2u : 3u;
    if (input_bit >= inputs) return false;
    const uint32_t count = 1u << inputs;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t j = i ^ (1u << input_bit);
        if (((code >> i) ^ (code >> j)) & 1u) return true;
    }
    return false;
}

bool VivanteBlitRopOps::RopBranchUsesSource(uint32_t rop, bool foreground) {
    const uint32_t type = (rop >> 20) & 3u;
    const uint32_t code = RopCode(rop, foreground);
    if (type == 1u)
        return RopTruthTableDependsOn(code, type, 1u);
    if (type >= 2u)
        return RopTruthTableDependsOn(code, type, 1u);
    return false;
}

bool VivanteBlitRopOps::RopBranchUsesPattern(uint32_t rop, bool foreground) {
    const uint32_t type = (rop >> 20) & 3u;
    const uint32_t code = RopCode(rop, foreground);
    if (type == 0u)
        return RopTruthTableDependsOn(code, type, 1u);
    if (type >= 2u)
        return RopTruthTableDependsOn(code, type, 2u);
    return false;
}

bool VivanteBlitRopOps::RopBranchUsesDestination(uint32_t rop, bool foreground) {
    const uint32_t type = (rop >> 20) & 3u;
    return RopTruthTableDependsOn(RopCode(rop, foreground), type, 0u);
}

bool VivanteBlitRopOps::RopUsesSource(uint32_t rop) {
    const uint32_t type = (rop >> 20) & 3u;
    return RopBranchUsesSource(rop, true) || (type == 3u && RopBranchUsesSource(rop, false));
}

bool VivanteBlitRopOps::RopUsesPattern(uint32_t rop) {
    const uint32_t type = (rop >> 20) & 3u;
    return RopBranchUsesPattern(rop, true) || (type == 3u && RopBranchUsesPattern(rop, false));
}

bool VivanteBlitRopOps::RopUsesDestination(uint32_t rop) {
    const uint32_t type = (rop >> 20) & 3u;
    return RopBranchUsesDestination(rop, true) || (type == 3u && RopBranchUsesDestination(rop, false));
}

VivanteBlitRopOps::MonoRopSelection VivanteBlitRopOps::SelectMonoRop(bool mono_one, bool mono_transparency_enabled,
                                                               bool mono_transparent_one) {
    if (mono_transparency_enabled) {
        const bool transparent = mono_one == mono_transparent_one;
        return {!transparent, true};
    }
    return {true, mono_one};
}

uint32_t VivanteBlitRopOps::ApplyRop(uint32_t rop, uint32_t dst, uint32_t src, uint32_t pat, bool foreground) {
    const uint32_t type = (rop >> 20) & 3u;
    const uint32_t code = RopCode(rop, foreground);


    uint32_t out = 0u;
    for (uint32_t bit = 0; bit < 32u; ++bit) {
        const uint32_t d = (dst >> bit) & 1u;
        const uint32_t s = (src >> bit) & 1u;
        const uint32_t p = (pat >> bit) & 1u;
        uint32_t idx = 0u;
        if (type == 0u)
            idx = d | (p << 1);
        else if (type == 1u)
            idx = d | (s << 1);
        else
            idx = d | (s << 1) | (p << 2);
        if ((code >> idx) & 1u) out |= 1u << bit;
    }
    return out;
}

uint32_t VivanteBlitRopOps::ReadStreamMonoBit(const uint8_t* stream, uint32_t x, uint32_t y, uint32_t width, uint32_t pack,
                                           uint32_t stream_words) {
    const uint32_t block_w = pack == 1u ? 16u : ((pack == 2u || pack == 3u) ? 32u : 8u);
    const uint32_t block_h = pack == 1u ? 2u : ((pack == 2u || pack == 3u) ? 1u : 4u);
    const uint32_t words_per_block_row = (width + block_w - 1u) / block_w;
    const uint32_t word_index = (y / block_h) * words_per_block_row + (x / block_w);
    if (stream_words != 0u && word_index >= stream_words) return 0u;
    const uint32_t in_word = (y % block_h) * block_w + (x % block_w);
    if (pack == 3u) {
        const uint8_t b = stream[word_index * 4u + (in_word >> 3)];
        return (b >> (7u - (in_word & 7u))) & 1u;
    }
    uint32_t w = 0;
    std::memcpy(&w, stream + word_index * 4u, sizeof(w));
    return (w >> (31u - in_word)) & 1u;
}

} // namespace imx6_vivante
