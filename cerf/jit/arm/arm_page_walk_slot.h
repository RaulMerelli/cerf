#pragma once

#include <cstdint>

struct ArmPageWalkSlot {
    uint32_t span_bytes    = 0x1000u;
    uint16_t par_attrs     = 0u;
    bool     global        = false;
    bool     fast_fillable = true;
};
