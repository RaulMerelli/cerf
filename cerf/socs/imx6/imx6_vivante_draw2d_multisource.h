#pragma once

#include <cstdint>

#include "imx6_vivante_draw2d_state.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"

namespace imx6_vivante {

class VivanteDraw2dMultiSource : protected VivanteDraw2dState {
protected:
    VivanteDraw2dMultiSource(VivanteState& s, VivanteMem& mem) : VivanteDraw2dState(s, mem) {}

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

    MultiSourceDesc LoadMultiSource(uint32_t index);
    void ReadMultiSource(const MultiSourceDesc& m, uint32_t x, uint32_t y, uint32_t& argb, uint32_t& packed);
    bool ExecuteMultiSourceRect(uint32_t raw_x0, uint32_t raw_y0, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h);
};

}
