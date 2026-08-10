#include "arm_interrupt_channel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../core/virtual_clock.h"
#include "../../core/virtual_timer_list.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_cpu.h"
#include "cpu_state.h"

REGISTER_SERVICE(ArmInterruptChannel);

ArmInterruptChannel::~ArmInterruptChannel() {
    if (idle_event_) {
        CloseHandle(idle_event_);
        idle_event_ = nullptr;
    }
}

void ArmInterruptChannel::OnReady() {
    cpu_state_        = emu_.Get<ArmCpu>().State();
    processor_config_ = &emu_.Get<ArmProcessorConfig>();

    idle_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!idle_event_) {
        LOG(Caution, "ArmInterruptChannel: CreateEventW(idle_event) failed "
                "gle=%lu\n", GetLastError());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

void ArmInterruptChannel::SetInterruptPending() {
    if (irq_line_.exchange(1u, std::memory_order_acq_rel) == 0u) {
        SetEvent(idle_event_);
    }
}

void ArmInterruptChannel::ClearInterruptPending() {
    irq_line_.store(0u, std::memory_order_release);
}

void ArmInterruptChannel::Wake() {
    SetEvent(idle_event_);
}

void __fastcall ArmInterruptChannel::WfiHelper(ArmInterruptChannel* channel) {
    ArmCpuState* state = channel->cpu_state_;
    if (state->reset_pending || state->deep_sleep) return;
    /* ARM DDI 0406C.c B1.8.14: a WFI wake-up event is "a physical IRQ
       interrupt, regardless of the value of the CPSR.I bit". */
    if (channel->irq_line_.load(std::memory_order_acquire) != 0u) return;

    VirtualClock&     clock  = channel->emu_.Get<VirtualClock>();
    VirtualTimerList& timers = channel->emu_.Get<VirtualTimerList>();

    const int64_t deadline = timers.NextDeadlineNs();

    clock.BeginIdleWait();
    WaitForSingleObject(channel->idle_event_, 1);
    const int64_t folded_ns = clock.EndIdleWait(deadline);

    state->guest_cycle_counter += static_cast<uint32_t>(VirtualClock::ScaleU64(
        static_cast<uint64_t>(folded_ns),
        channel->processor_config_->CpuClockHz(), 1000000000ull));

    timers.RunExpired(VirtualTimerList::Site::Wfi);
}
