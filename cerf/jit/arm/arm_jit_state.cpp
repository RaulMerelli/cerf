#include "arm_jit.h"

#include "../../core/cerf_emulator.h"
#include "arm_cpu.h"
#include "arm_mmu.h"

void ArmJit::SaveCpuState(StateWriter& w)    { emu_.Get<ArmCpu>().SaveState(w); }
void ArmJit::RestoreCpuState(StateReader& r) { emu_.Get<ArmCpu>().RestoreState(r); }
void ArmJit::SaveMmuState(StateWriter& w)    { emu_.Get<ArmMmu>().SaveState(w); }
void ArmJit::RestoreMmuState(StateReader& r) { emu_.Get<ArmMmu>().RestoreState(r); }

void ArmJit::FlushTranslationCache() {
    arena_.Flush();
    blocks_arm_.FlushAll();
    blocks_thumb_.FlushAll();
    shadow_stack_count_ = 0;
}

void ArmJit::SetInjectionBand(uint32_t va, uint32_t pa, uint32_t size) {
    emu_.Get<ArmMmu>().SetInjectionBand(va, pa, size);
}

void ArmJit::SetDmaRegion(uint32_t /*pa*/, uint32_t /*size*/) {}
