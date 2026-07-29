#pragma once

#include "service.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class VirtualTimerList : public Service {
public:
    using Service::Service;

    static constexpr int64_t kNoDeadline = INT64_MAX;

    enum class Site : uint8_t {
        RunLoop = 0,
        Wfi,
        Poll,
        Count,
    };

    class Entry {
    public:
        explicit Entry(std::function<void()> fn) : fn_(std::move(fn)) {}

        void Arm(int64_t deadline_ns) {
            deadline_ns_ = deadline_ns;
            owner_->RecomputeNextTsc();
        }
        int64_t DeadlineNs() const { return deadline_ns_; }

    private:
        friend class VirtualTimerList;
        VirtualTimerList*     owner_       = nullptr;
        int64_t               deadline_ns_ = kNoDeadline;
        std::function<void()> fn_;
    };

    void OnReady() override;

    Entry* Add(std::function<void()> fn);

    int64_t NextDeadlineNs() const;

    void RunExpired(Site site);

    const uint64_t* NextDeadlineTscAddr() const { return &next_deadline_tsc_; }

private:
    void RecomputeNextTsc();
    void Recalibrate(uint64_t tsc);

    uint64_t tsc_per_sec_       = 1;
    uint64_t tsc_hz_boot_       = 1;
    uint64_t next_deadline_tsc_ = UINT64_MAX;
    uint64_t qpf_               = 1;
    uint64_t cal_tsc_           = 0;
    int64_t  cal_qpc_           = 0;

    std::vector<std::unique_ptr<Entry>> entries_;
};
