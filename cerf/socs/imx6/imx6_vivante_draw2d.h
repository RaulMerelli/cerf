#pragma once

#include <cstdint>

#include "imx6_vivante_draw2d_multisource.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"

namespace imx6_vivante {

class VivanteDraw2d : protected VivanteDraw2dMultiSource {
public:
    VivanteDraw2d(VivanteState& s, VivanteMem& mem) : VivanteDraw2dMultiSource(s, mem) {}

    void Execute(const uint32_t* rect_words, uint32_t rect_count, const uint32_t* stream_data, uint32_t stream_words);

private:
    void ExecuteLine(uint32_t rect_x0, uint32_t rect_y0, uint32_t rect_x1, uint32_t rect_y1);
    void ExecuteMonoStream(const uint8_t* stream, uint32_t stream_words, uint32_t rect_x0, uint32_t rect_y0,
                           uint32_t rect_w, uint32_t rect_h, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h);
    void ExecuteCopyLike(uint32_t rect_x0, uint32_t rect_y0, uint32_t rect_w, uint32_t rect_h, uint32_t x0, uint32_t y0,
                         uint32_t w, uint32_t h);
    void ExecutePatternFill(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h);
};

}
