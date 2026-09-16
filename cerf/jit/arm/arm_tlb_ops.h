#pragma once

#include <cstdint>

#include "arm_mmu_state.h"
#include "arm_par_attributes.h"

void ArmTlbFlushAll(ArmTlbUnit* unit);

struct ArmTlbInvalidation {
    uint32_t base;
    uint32_t span_bytes;
};

/* ARM DDI 0406C.d B3.10.1; DDI 0406C.c B3.19.2. */
ArmTlbInvalidation ArmTlbInvalidateByVa(ArmTlbUnit* unit,
                                        uint32_t process_id, uint32_t va);

struct ArmTlbFillSlot {
    uint32_t span_bytes = 0x1000u;
    uint16_t par_attrs = 0u;
    bool global = false;
    bool fast_fillable = true;
};

void FillFastTlb(ArmTlbUnit* unit, uint32_t folded_va, uint8_t* host,
                 uint32_t pa, uint8_t asid,
                 const ArmTlbFillSlot& slot, bool writable);

void FillFastTlbIo(ArmTlbUnit* unit, uint32_t folded_va, uint32_t pa,
                   uint8_t asid, const ArmTlbFillSlot& slot, bool writable);
