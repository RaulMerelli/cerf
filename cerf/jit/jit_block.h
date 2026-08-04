#pragma once

#include <cstdint>

class JitBlockIndex;

struct JitBlock {
    uint32_t guest_start  = 0;
    uint32_t guest_end    = 0;
    uint32_t phys_start   = 0;

    uint8_t* native_start = nullptr;

    JitBlockIndex* owner  = nullptr;

    JitBlock* page_next[2] = {nullptr, nullptr};
    uint32_t  index_start  = 0;
    uint32_t  index_split  = 0;
    uint32_t  index_start2 = 0;
};
