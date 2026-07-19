#include "mips_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../host/guest_deep_sleep.h"

void MipsJit::EnterDeepSleep() {
    cpu_state_.deep_sleep = 1;
}

void MipsJit::ExitDeepSleep() {
    cpu_state_.deep_sleep = 0;
    SignalIdleWake();
}

void __fastcall MipsJit::HibernateHelper(uint32_t next_pc, MipsJit* jit) {
    jit->cpu_state_.pc = next_pc;
    jit->emu_.Get<GuestDeepSleep>().Enter();
}

void __fastcall MipsJit::WaitHelper(MipsJit* jit) {
    MipsCpuState& s = jit->cpu_state_;
    if (s.reset_pending || s.deep_sleep) return;
    /* Standby/Suspend halt the CPU core until any interrupt; RTC+ICU keep running
       (VR4102 UM ch.27 p643/p646, Table 15-3 p326). */
    if (jit->external_ip_.load(std::memory_order_acquire) & jit->device_ip_mask_) return;
    WaitForSingleObject(jit->idle_event_, 1);
}
