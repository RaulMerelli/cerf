#include "arm_jit.h"

#include "arm_mmu.h"

uint8_t* __fastcall ArmJit::TranslateReadHelper(uint32_t va, ArmJit* jit) {
    return jit->mmu_->TranslateRead(jit->cpu_state_, va);
}

uint8_t* __fastcall ArmJit::TranslateWriteHelper(uint32_t va, ArmJit* jit) {
    return jit->mmu_->TranslateWrite(jit->cpu_state_, va);
}

uint8_t* __fastcall ArmJit::TranslateReadWriteHelper(uint32_t va, ArmJit* jit) {
    return jit->mmu_->TranslateReadWrite(jit->cpu_state_, va);
}
