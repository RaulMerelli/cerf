#include "imx6_vivante_blit.h"

namespace imx6_vivante {

VivanteBlit::VivanteBlit(VivanteState& s, VivanteMem& mem) : draw2d_(s, mem), vr_(s, mem), rs_(s, mem) {}

void VivanteBlit::ExecuteDraw2d(const uint32_t* rect_words, uint32_t rect_count, const uint32_t* stream_data,
                                uint32_t stream_words) {
    draw2d_.Execute(rect_words, rect_count, stream_data, stream_words);
}

void VivanteBlit::ExecuteVideoRasterizer(uint32_t start_value) { vr_.Execute(start_value); }

void VivanteBlit::ExecuteRs() { rs_.ExecuteRs(); }

void VivanteBlit::ExecuteRsInPlace(uint32_t tile_count) { rs_.ExecuteRsInPlace(tile_count); }

}
