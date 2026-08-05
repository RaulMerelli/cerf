#include "arm_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../host/guest_deep_sleep.h"
#include "arm_cpu.h"

void ArmJit::EnterDeepSleep() {
    /* SA-1110 §9.5.3: PMCR.SF halts the CPU until a wake reset. */
    cpu_->State()->deep_sleep = 1;
}

void ArmJit::ExitDeepSleep() {
    cpu_->State()->deep_sleep = 0;
    SetEvent(idle_event_);
}

void __fastcall ArmJit::EnterDeepSleepHelper(ArmJit* jit) {
    jit->emu_.Get<GuestDeepSleep>().Enter();
}
