#pragma once

#include "imx6_vivante_mmu.h"

#include <cstdint>

namespace imx6_vivante {

class VivanteMem;

class VivanteStateRegisters {
public:
    VivanteStateRegisters(VivanteState& state, VivanteMmu& mmu,
                          VivanteMem& memory)
        : state_(state), mmu_(mmu), memory_(memory) {}

    void Store(uint32_t byte_offset, uint32_t value);

private:
    VivanteState& state_;
    VivanteMmu& mmu_;
    VivanteMem& memory_;
};

} // namespace imx6_vivante
