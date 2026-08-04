#pragma once

#include <cstdint>

#include "arm_mmu_state.h"

void ArmTlbFlushAll(ArmTlbUnit* unit);

/* Returns the FCSE-folded VA (ARM ARM DDI 0406C.c B3.19.2, p. B3-1503). */
uint32_t ArmTlbInvalidateByVa(ArmTlbUnit* unit, uint32_t process_id, uint32_t va);

void FillFastTlb(ArmTlbUnit* unit, uint32_t folded_va, uint8_t* host,
                 uint32_t pa, uint8_t asid, bool global, bool writable);

void FillFastTlbIo(ArmTlbUnit* unit, uint32_t folded_va, uint32_t pa,
                   uint8_t asid, bool global, bool writable);
