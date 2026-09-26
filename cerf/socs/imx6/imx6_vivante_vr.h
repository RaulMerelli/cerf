#pragma once

#include <cstdint>

#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_surface_access.h"

namespace imx6_vivante {

class VivanteVr : protected VivanteSurfaceAccess {
public:
    VivanteVr(VivanteState& s, VivanteMem& mem) : VivanteSurfaceAccess(mem), s_(s) {}

    void Execute(uint32_t start_value);

private:
    VivanteState& s_;
};

}
