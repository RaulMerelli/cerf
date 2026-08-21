#pragma once

#include "service.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class VirtualTimerList : public Service {
public:
    using Service::Service;

    static constexpr int64_t kNoDeadline = INT64_MAX;

    class Entry {
    public:
        explicit Entry(std::function<void()> fn) : fn_(std::move(fn)) {}

        void Arm(int64_t deadline_ns) {
            owner_->ArmEntry(this, deadline_ns);
        }
        int64_t DeadlineNs() const;

    private:
        friend class VirtualTimerList;
        VirtualTimerList*     owner_       = nullptr;
        int64_t               deadline_ns_ = kNoDeadline;
        std::function<void()> fn_;
    };

    void OnReady() override;
    void OnShutdown() override;
    ~VirtualTimerList() override;

    Entry* Add(std::function<void()> fn);

private:
    void RunExpired();
    void ArmEntry(Entry* e, int64_t deadline_ns);
    int64_t NextDeadlineLocked() const;
    void ExpiryLoop();

    std::mutex  mtx_;
    std::thread thread_;
    std::atomic<bool>       stop_{false};
    std::atomic<uint32_t>   wake_gen_{0};
    void*                   timer_      = nullptr;
    void*                   wake_event_ = nullptr;

    int64_t waiting_until_ = kNoDeadline;

    std::vector<std::unique_ptr<Entry>> entries_;
};
