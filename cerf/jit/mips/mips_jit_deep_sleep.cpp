#include "mips_jit.h"

#include "../../core/cerf_emulator.h"
#include "../../host/guest_deep_sleep.h"

void MipsJit::EnterDeepSleep() {
    cpu_state_.deep_sleep = 1;
}

void __fastcall MipsJit::HibernateHelper(uint32_t next_pc, MipsJit* jit) {
    jit->cpu_state_.pc = next_pc;
    jit->emu_.Get<GuestDeepSleep>().Enter();
}
