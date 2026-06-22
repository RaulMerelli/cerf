#pragma once

#include <cstdint>

#include "arm_mmu_state.h"

void ArmTlbFlushAll(ArmTlbUnit* unit);

void ArmTlbInvalidateByVa(ArmTlbUnit* unit, uint32_t process_id, uint32_t va);
