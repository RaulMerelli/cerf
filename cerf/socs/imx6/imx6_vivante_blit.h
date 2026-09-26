#pragma once

#include <cstdint>

#include "imx6_vivante_draw2d.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_rs.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_vr.h"

namespace imx6_vivante {

class VivanteBlit {
public:
    VivanteBlit(VivanteState& s, VivanteMem& mem);

    void ExecuteDraw2d(const uint32_t* rect_words, uint32_t rect_count, const uint32_t* stream_data,
                       uint32_t stream_words);
    void ExecuteVideoRasterizer(uint32_t start_value);
    void ExecuteRs();
    void ExecuteRsInPlace(uint32_t tile_count);

private:
    VivanteDraw2d draw2d_;
    VivanteVr vr_;
    VivanteRs rs_;
};

}
