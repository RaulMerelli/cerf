#include "arm_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>

void ArmJit::SetInterruptPending() {
    if (irq_line_.exchange(1u, std::memory_order_acq_rel) == 0u) {
        SetEvent(idle_event_);
    }
}

void ArmJit::ClearInterruptPending() {
    irq_line_.store(0u, std::memory_order_release);
}
