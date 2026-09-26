#pragma once

#include <cstdint>

#include "imx6_vivante_blit.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"

namespace imx6_vivante {

class VivanteFe {
public:
    VivanteFe(VivanteState& s, VivanteMem& mem, VivanteBlit& blit) : s_(s), mem_(mem), blit_(blit) {}

    void AdvanceFrontendRing();
    void RunFrontend(uint32_t control);

private:
    bool CommandFits(uint32_t command_words, uint32_t window_words, uint32_t pc, FeStats& stats);
    bool FetchWords(uint32_t pc, uint32_t* words, uint32_t count, FeCommandAddressSpace address_space,
                    uint32_t window_words, FeStats& stats);
    void ConsumeWords(uint32_t words, uint32_t& pc, uint32_t& window_words) const;
    uint32_t ExecuteCommandStream(uint32_t address, uint32_t prefetch, FeStats& stats, bool resume,
                                  FeCommandAddressSpace start_space);
    void ApplyLoadState(const uint32_t* values, uint32_t state_word, uint32_t count, bool fixp);

    VivanteState& s_;
    VivanteMem& mem_;
    VivanteBlit& blit_;
};

}
