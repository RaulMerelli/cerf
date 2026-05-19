#pragma once

#include <cstdint>

struct JitBlock {
    uint32_t   guest_start;
    uint32_t   guest_end;
    void*      native_start;
    void*      native_end;
    uint32_t   flags_needed;
    JitBlock*  sub_block;
};
