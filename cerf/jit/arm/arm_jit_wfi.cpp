#include "arm_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>

#include "../../core/cerf_emulator.h"
#include "../../core/virtual_clock.h"
#include "../../core/virtual_timer_list.h"
#include "../../cpu/arm_processor_config.h"
#include "cpu_state.h"

void __fastcall ArmJit::WfiHelper(ArmJit* jit) {
    ArmCpuState* state = jit->cpu_state_;
    if (state->reset_pending || state->deep_sleep) return;
    /* ARM DDI 0406C.c B1.8.14: a WFI wake-up event is "a physical IRQ
       interrupt, regardless of the value of the CPSR.I bit". */
    if (jit->irq_line_.load(std::memory_order_acquire) != 0u) return;

    VirtualClock&     clock  = jit->emu_.Get<VirtualClock>();
    VirtualTimerList& timers = jit->emu_.Get<VirtualTimerList>();

    const int64_t deadline = timers.NextDeadlineNs();

    clock.BeginIdleWait();
    WaitForSingleObject(jit->idle_event_, 1);
    const int64_t folded_ns = clock.EndIdleWait(deadline);

    state->guest_cycle_counter += static_cast<uint32_t>(VirtualClock::ScaleU64(
        static_cast<uint64_t>(folded_ns),
        jit->processor_config_->CpuClockHz(), 1000000000ull));

    timers.RunExpired(VirtualTimerList::Site::Wfi);
}
