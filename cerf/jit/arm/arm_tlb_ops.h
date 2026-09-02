#pragma once

#include <cstdint>

#include "arm_mmu_state.h"
#include "arm_par_attributes.h"

void ArmTlbFlushAll(ArmTlbUnit* unit);

/* Returns the FCSE-folded VA (ARM ARM DDI 0406C.c B3.19.2, p. B3-1503). */
uint32_t ArmTlbInvalidateByVa(ArmTlbUnit* unit, uint32_t process_id, uint32_t va);

/* What a walk decided about the page it just resolved, before the entry
   exists: span, PAR attributes, and the two flags the entry packs. */
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
