#pragma once

#include "imx6_vivante_state.h"

#include <cstdint>

namespace imx6_vivante {

class VivanteMem;

class VivanteStateRegisters {
public:
    VivanteStateRegisters(VivanteState& state, VivanteMem& memory)
        : state_(state), memory_(memory) {}

    static bool SupportsOffset(uint32_t byte_offset);
    void Store(uint32_t byte_offset, uint32_t value);

private:
    VivanteState& state_;
    VivanteMem& memory_;
};

}
