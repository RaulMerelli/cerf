#pragma once

#include "../core/service.h"

#include <atomic>
#include <cstdint>

/* User pause: freezes the guest CPU via JitRunner::Pause/Resume and signals the
   host present/UART layers to render a paused state. Toggle on the UI thread
   only - JitRunner::Pause self-deadlocks if called from the JIT thread. */
class EmulationPause : public Service {
public:
    using Service::Service;

    void Toggle();
    void SetPaused(bool paused);
    bool IsPaused() const { return paused_.load(std::memory_order_acquire); }

    /* Wall-clock ms for host animations, frozen while paused so the UART boot
       bar stops advancing. */
    uint64_t AnimationTickMs() const;

private:
    std::atomic<bool>     paused_{false};
    std::atomic<uint64_t> pause_tick_ms_{0};
};
