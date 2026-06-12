#define NOMINMAX

#include "emulation_pause.h"

#include "../core/cerf_emulator.h"
#include "../jit/jit_runner.h"

#include <windows.h>

REGISTER_SERVICE(EmulationPause);

void EmulationPause::Toggle() { SetPaused(!IsPaused()); }

void EmulationPause::SetPaused(bool paused) {
    if (paused == paused_.load(std::memory_order_acquire)) return;
    auto& runner = emu_.Get<JitRunner>();
    if (paused) {
        pause_tick_ms_.store(GetTickCount64(), std::memory_order_release);
        runner.Pause();
    } else {
        runner.Resume();
    }
    paused_.store(paused, std::memory_order_release);
}

uint64_t EmulationPause::AnimationTickMs() const {
    return IsPaused() ? pause_tick_ms_.load(std::memory_order_acquire)
                      : GetTickCount64();
}
