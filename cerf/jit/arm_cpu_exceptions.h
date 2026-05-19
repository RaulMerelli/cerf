#pragma once

#include <cstdint>

#include "cpu_state.h"

class ArmJit;

void* ArmCpuRaiseUndefinedException     (ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr);
void* ArmCpuRaiseAbortDataException     (ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr);
void* ArmCpuRaiseAbortPrefetchException (ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr);
void* ArmCpuRaiseIrqException           (ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr);
void* ArmCpuRaiseSoftwareInterruptException(ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr);
