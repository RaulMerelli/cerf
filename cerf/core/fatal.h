#pragma once

#include "service.h"

#include <atomic>

class GuestEngine;

class Fatal : public Service {
public:
    using Service::Service;

    [[noreturn]] void Die(const char* fmt, ...);

    void SetLiveEngine(GuestEngine* engine) {
        live_engine_.store(engine, std::memory_order_release);
    }

private:
    std::atomic<GuestEngine*> live_engine_{nullptr};
};
