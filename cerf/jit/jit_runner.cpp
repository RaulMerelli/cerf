#include "jit_runner.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../core/rate_probe.h"
#include "arm_jit.h"

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
        if (pause_requested_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lk(pause_mutex_);
            paused_ = true;
            pause_cv_.notify_all();
            pause_cv_.wait(lk, [this] {
                return !pause_requested_.load(std::memory_order_acquire) ||
                       stop_requested_.load(std::memory_order_acquire);
            });
            paused_ = false;
        }
    }

    LOG(Boot, "JitRunner: stop requested; thread exiting\n");
    stopped_.store(true, std::memory_order_release);
    { std::lock_guard<std::mutex> lk(pause_mutex_); }
    pause_cv_.notify_all();
}
