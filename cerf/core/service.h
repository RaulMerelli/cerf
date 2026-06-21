#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <typeinfo>

class CerfEmulator;
class Service;

namespace ServiceInternal {
    [[noreturn]] void HaltOnCycle(const std::type_info& ti);
}

class Service {
protected:
    CerfEmulator& emu_;

private:
    std::atomic<bool>            ready_{false};
    std::atomic<bool>            shutdown_ran_{false};
    std::mutex                   ready_mtx_;
    std::atomic<std::thread::id> owner_{};

public:
    explicit Service(CerfEmulator& emu) : emu_(emu) {}
    virtual ~Service() = default;

    virtual void OnReady() {}
    virtual bool ShouldRegister() { return true; }

    /* Runs for every service before any destructor: stop worker threads /
       detach from peers ONLY. Freeing a buffer another service's thread reads
       must stay in the destructor - this phase is unordered, so a free here
       can still race a peer thread that a later OnShutdown hasn't stopped yet. */
    virtual void OnShutdown() {}

    void RunShutdown() {
        if (!ready_.load(std::memory_order_acquire)) return;
        bool expected = false;
        if (!shutdown_ran_.compare_exchange_strong(expected, true)) return;
        OnShutdown();
    }

    void EnsureReady() {
        if (ready_.load(std::memory_order_acquire)) return;
        if (owner_.load(std::memory_order_acquire) == std::this_thread::get_id()) {
            ServiceInternal::HaltOnCycle(typeid(*this));
        }
        std::lock_guard<std::mutex> lk(ready_mtx_);
        if (ready_.load(std::memory_order_relaxed)) return;
        owner_.store(std::this_thread::get_id(), std::memory_order_release);
        OnReady();
        ready_.store(true, std::memory_order_release);
        owner_.store(std::thread::id{}, std::memory_order_release);
    }
};
