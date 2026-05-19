#pragma once

#include <atomic>
#include <thread>

#include "../core/service.h"

class JitRunner : public Service {
public:
    using Service::Service;
    ~JitRunner() override;

    /* Spawn the JIT thread. Idempotent: subsequent calls are no-ops. */
    void Start();

    /* Block until the JIT thread terminates. Caller is expected to
       arrange termination (RequestStop or process exit) before
       calling — Join itself does NOT signal stop. */
    void Join();

    /* Cooperative stop signal. The thread checks this between
       ArmJit::Run() iterations and exits when set. Safe to call
       from any thread. */
    void RequestStop() { stop_requested_.store(true, std::memory_order_release); }

private:
    void RunLoop();

    std::thread       thread_;
    std::atomic<bool> stop_requested_{false};
    bool              started_ = false;
};
