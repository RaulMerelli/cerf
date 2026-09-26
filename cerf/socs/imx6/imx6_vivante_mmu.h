#pragma once

#include "imx6_vivante_state.h"

#include <cstdint>

class CerfEmulator;

namespace imx6_vivante {

class VivanteMmu {
public:
    VivanteMmu(VivanteState& state, CerfEmulator& emu)
        : state_(state), emu_(emu) {}

    const uint8_t* TranslateToHost(
        uint32_t gpu_address,
        MmuClient client = MmuClient::Texture) const;
    uint8_t* TranslateToHostWrite(
        uint32_t gpu_address,
        MmuClient client = MmuClient::PixelEngine) const;

private:
    static uint32_t PageTableRegister(MmuClient client);
    static uint32_t MemoryBaseRegister(MmuClient client);
    bool ReadPhysicalU32(uint32_t address, uint32_t& value) const;
    bool TranslateMmuv1(uint32_t gpu_address, MmuClient client,
                        uint32_t& physical) const;

    VivanteState& state_;
    CerfEmulator& emu_;
};

}
