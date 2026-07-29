#pragma once

#include "service.h"

#include <atomic>
#include <cstdint>

class VirtualClock : public Service {
public:
    using Service::Service;

    void OnReady() override;

    int64_t NowNs() const;

    void Pause();
    void Resume();

    void    BeginIdleWait();
    int64_t EndIdleWait(int64_t cap_deadline_ns);

    static uint64_t ScaleU64(uint64_t value, uint64_t num, uint64_t den) {
        return (value / den) * num + ((value % den) * num) / den;
    }

private:
    int64_t HostTicks() const;

    std::atomic<uint32_t> seq_{0};
    std::atomic<int64_t>  accum_ticks_{0};
    std::atomic<int64_t>  epoch_ticks_{0};
    std::atomic<bool>     running_{true};
    int64_t               host_hz_ = 1;
    int64_t               idle_wait_start_ticks_ = 0;
    int64_t               deficit_ticks_ = 0;
};
