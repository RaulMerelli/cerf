#pragma once

#include <cstdint>

#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_surface_access.h"

namespace imx6_vivante {

class VivanteRs : protected VivanteSurfaceAccess {
public:
    VivanteRs(VivanteState& s, VivanteMem& mem) : VivanteSurfaceAccess(mem), s_(s) {}

    void ExecuteRsInPlace(uint32_t tile_count);
    void ExecuteRs();

private:
    VivanteState& s_;
};

}
