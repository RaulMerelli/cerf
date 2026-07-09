#include "arm_jit.h"

#include "../../core/cerf_emulator.h"
#include "../../host/guest_deep_sleep.h"
#include "arm_cpu.h"

#include <mutex>

void ArmJit::EnterDeepSleep() {
    /* SA-1110 §9.5.3: PMCR.SF halts the CPU until a wake reset; re-arm the poll so it returns to the dispatcher (RunLoop parks). */
    std::lock_guard<std::mutex> guard(interrupt_lock_);
    cpu_->State()->deep_sleep = 1;
    UpdateInterruptOnPoll();
}

void __fastcall ArmJit::EnterDeepSleepHelper(ArmJit* jit) {
    jit->emu_.Get<GuestDeepSleep>().Enter();
}
