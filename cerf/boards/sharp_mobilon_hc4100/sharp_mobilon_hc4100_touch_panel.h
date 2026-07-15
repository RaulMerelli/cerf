#pragma once

#include "../../core/service.h"

#include <atomic>
#include <cstdint>

class SharpMobilonHc4100TouchPanel : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;

    void SetPen(bool down, int x, int y);
    bool Down() const { return down_.load(std::memory_order_acquire); }
    int  X()    const { return x_.load(std::memory_order_relaxed); }
    int  Y()    const { return y_.load(std::memory_order_relaxed); }

    void     SetPenIrqArmed(uint16_t bits) { pen_irq_armed_.store(bits, std::memory_order_release); }
    void     ClearPenIrq(uint16_t mask);
    uint16_t PenIrqStatus() const { return irq_status_.load(std::memory_order_acquire); }

private:
    void AssertPenIrq();

    std::atomic<bool> down_{false};
    std::atomic<int>  x_{0}, y_{0};
    std::atomic<uint16_t> pen_irq_armed_{0};
    std::atomic<uint16_t> irq_status_{0};
};
