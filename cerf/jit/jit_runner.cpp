#include "jit_runner.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../core/rate_probe.h"
#include "arm_jit.h"
#include "cpu_state.h"

#include <chrono>
#include <intrin.h>

#if CERF_DEV_MODE
#include "../tracing/trace_manager.h"
#include "arm_cpu.h"
#include "arm_cpu_ops.h"
#include "cpu_state.h"
#endif

REGISTER_SERVICE(JitRunner);

JitRunner::~JitRunner() {
    /* If the owner forgot to RequestStop+Join, do it here so the
       std::thread destructor doesn't call terminate(). */
    if (thread_.joinable()) {
        stop_requested_.store(true, std::memory_order_release);
        thread_.join();
    }
}

void JitRunner::Start() {
    if (started_) return;
    /* Hooks run with started_ still false so a restore's JitRunner::Pause
       is a no-op and writes state into the not-yet-running guest. */
    for (auto& h : pre_start_hooks_) h();
    started_ = true;
    thread_ = std::thread([this] { RunLoop(); });
}

void JitRunner::RegisterPreStartHook(std::function<void()> fn) {
    pre_start_hooks_.push_back(std::move(fn));
}

void JitRunner::Join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void JitRunner::RequestStop() {
    stop_requested_.store(true, std::memory_order_release);
    /* Wake the JIT thread if it is parked in a pause wait so it can
       observe the stop and exit. */
    { std::lock_guard<std::mutex> lk(pause_mutex_); }
    pause_cv_.notify_all();
}

void JitRunner::Pause() {
    if (!started_) return;
    std::unique_lock<std::mutex> lk(pause_mutex_);
    pause_requested_.store(true, std::memory_order_release);
    pause_cv_.wait(lk, [this] {
        return paused_ || stopped_.load(std::memory_order_acquire);
    });
}

void JitRunner::Resume() {
    {
        std::lock_guard<std::mutex> lk(pause_mutex_);
        pause_requested_.store(false, std::memory_order_release);
    }
    pause_cv_.notify_all();
}

void JitRunner::RunLoop() {
    LOG(Jit, "JitRunner::RunLoop: entered, resolving ArmJit\n");
    /* Resolve ArmJit lazily on the JIT thread — first Get<T> walks the
       OnReady dependency chain. A Get<ArmJit>() in JitRunner::OnReady
       is service pre-warming, forbidden by agent_docs/rules.md. */
    auto& jit = emu_.Get<ArmJit>();
    LOG(Jit, "JitRunner::RunLoop: ArmJit resolved, entering loop\n");

#if CERF_DEV_MODE
    auto&        probe = emu_.Get<RateProbe>();
    auto&        tm    = emu_.Get<TraceManager>();
    ArmCpuState* state = jit.CpuState();
#endif

    bool prev_deep_sleep = false;
    while (!stop_requested_.load(std::memory_order_acquire)) {
#if CERF_DEV_MODE
        const uint64_t t0 = __rdtsc();
        jit.Run();
        probe.AddTsc(RateProbe::TimeCounter::JitRun, __rdtsc() - t0);
        probe.Inc(RateProbe::Counter::JitRuns);
        tm.DispatchRunLoopIter(state->gprs, ArmCpuGetCpsrWithFlags(state));
#else
        jit.Run();
#endif
        ArmCpuState* cpu = jit.CpuState();
        const bool ds = cpu->deep_sleep != 0;
        if (ds != prev_deep_sleep) {
            LOG(SocReset, "[DEEPSLEEP] RunLoop: deep_sleep %d->%d reset_pending=%u pause=%d\n",
                prev_deep_sleep, ds, cpu->reset_pending,
                static_cast<int>(pause_requested_.load(std::memory_order_acquire)));
            prev_deep_sleep = ds;
        }
        if (pause_requested_.load(std::memory_order_acquire) || cpu->deep_sleep) {
            std::unique_lock<std::mutex> lk(pause_mutex_);
            paused_ = true;
            pause_cv_.notify_all();
            LOG(SocReset, "[DEEPSLEEP] RunLoop: park enter ds=%u reset_pending=%u pause=%d pc=0x%08X\n",
                cpu->deep_sleep, cpu->reset_pending,
                static_cast<int>(pause_requested_.load(std::memory_order_acquire)),
                cpu->gprs[15]);
            /* Bounded wait: the wake (reset_pending) is signalled via idle_event_,
               not pause_cv_, so an unbounded wait would never observe it and the
               deep-sleep park would never wake. */
            while (!stop_requested_.load(std::memory_order_acquire) &&
                   !cpu->reset_pending &&
                   (pause_requested_.load(std::memory_order_acquire) || cpu->deep_sleep)) {
                pause_cv_.wait_for(lk, std::chrono::milliseconds(20));
            }
            paused_ = false;
            LOG(SocReset, "[DEEPSLEEP] RunLoop: park exit ds=%u reset_pending=%u pause=%d pc=0x%08X\n",
                cpu->deep_sleep, cpu->reset_pending,
                static_cast<int>(pause_requested_.load(std::memory_order_acquire)),
                cpu->gprs[15]);
        }
    }

    LOG(Boot, "JitRunner: stop requested; thread exiting\n");
    stopped_.store(true, std::memory_order_release);
    { std::lock_guard<std::mutex> lk(pause_mutex_); }
    pause_cv_.notify_all();
}
