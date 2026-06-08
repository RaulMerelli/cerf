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
    started_ = true;
    thread_ = std::thread([this] { RunLoop(); });
}

void JitRunner::Join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void JitRunner::RunLoop() {
    LOG(Jit, "JitRunner::RunLoop: entered, resolving ArmJit\n");
    /* Lazy ArmJit resolution — the service locator's first Get<T>
       runs the chain of OnReady's in dependency order. Doing it
       here (rather than JitRunner::OnReady) avoids the forbidden
       "service pre-warming" pattern. */
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
    }

    LOG(Boot, "JitRunner: stop requested; thread exiting\n");
    stopped_.store(true, std::memory_order_release);
}
