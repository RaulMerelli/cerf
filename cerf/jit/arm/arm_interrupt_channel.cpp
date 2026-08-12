#include "arm_interrupt_channel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/arm_processor_config.h"

#include <chrono>
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
    const bool edge = irq_line_.exchange(1u, std::memory_order_acq_rel) == 0u;
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_or(kChainExitIrq, std::memory_order_acq_rel);
    if (edge) {
        SetEvent(idle_event_);
    }
}

void ArmInterruptChannel::ClearInterruptPending() {
    irq_line_.store(0u, std::memory_order_release);
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_and(~kChainExitIrq, std::memory_order_acq_rel);
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

    const auto start = std::chrono::steady_clock::now();
    WaitForSingleObject(channel->idle_event_, 1);
    const uint64_t elapsed_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
    const uint64_t cpu_hz = channel->processor_config_->CpuClockHz();
    state->guest_cycle_counter += static_cast<uint32_t>(
        (elapsed_ns * cpu_hz) / 1000000000ull);
}
