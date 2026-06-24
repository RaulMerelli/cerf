#include "arm_jit.h"

#include "../../core/cerf_emulator.h"
#include "arm_cpu.h"
#include "arm_mmu.h"

void ArmJit::SaveCpuState(StateWriter& w)    { emu_.Get<ArmCpu>().SaveState(w); }
void ArmJit::RestoreCpuState(StateReader& r) { emu_.Get<ArmCpu>().RestoreState(r); }
void ArmJit::SaveMmuState(StateWriter& w)    { emu_.Get<ArmMmu>().SaveState(w); }
void ArmJit::RestoreMmuState(StateReader& r) { emu_.Get<ArmMmu>().RestoreState(r); }
