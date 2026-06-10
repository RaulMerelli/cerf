#pragma once

#include "../../core/service.h"

#include <atomic>

/* Shared SIMpad pen state: the TouchInput adapter writes host pen events, the
   UCB1300 codec reads them for MCP ADC samples. Separate Get-able holder because
   both of those register AS their base and so can't reach each other by type. */
class SimpadSl4TouchPanel : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;

    void SetPen(bool down, int x, int y) {
        x_.store(x, std::memory_order_relaxed);
        y_.store(y, std::memory_order_relaxed);
        down_.store(down, std::memory_order_release);
    }
    bool Down() const { return down_.load(std::memory_order_acquire); }
    int  X()    const { return x_.load(std::memory_order_relaxed); }
    int  Y()    const { return y_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> down_{false};
    std::atomic<int>  x_{0}, y_{0};
};
