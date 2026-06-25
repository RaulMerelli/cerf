#include "arm_jit.h"

#include "arm_mmu.h"

std::optional<uint8_t*> ArmJit::PeekGuestVa(uint32_t va) {
    return mmu_->PeekDataTlb(va);
}

uint8_t* ArmJit::ResolveGuestVaToHost(uint32_t va) {
    return mmu_->PeekVaToHost(va);
}
