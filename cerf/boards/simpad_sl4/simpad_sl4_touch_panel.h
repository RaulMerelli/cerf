#pragma once

#include "../../core/service.h"

#include <atomic>
#include <cstdint>

class SimpadSl4TouchPanel : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;

    void SetPen(bool down, int x, int y);   /* asserts the pen IRQ on a down edge */
    bool Down() const { return down_.load(std::memory_order_acquire); }
    int  X()    const { return x_.load(std::memory_order_relaxed); }
    int  Y()    const { return y_.load(std::memory_order_relaxed); }

    /* UCB pen-detect IRQ (UCB_IE_TSPX bit12 -> UCB IRQ pin -> GPIO22 -> SYSINTR 24).
       touch.dll arms it via IE_FAL each sample cycle and sleeps on it while the pen
       is up; without the assert its IST only wakes on its ~505ms idle timeout. */
    void     SetPenIrqArmed(bool armed) { pen_irq_armed_.store(armed, std::memory_order_release); }
    void     ClearPenIrq(uint16_t mask);                 /* IE_CLEAR write */
    uint16_t PenIrqStatus() const { return irq_status_.load(std::memory_order_acquire); }

private:
    void AssertPenIrq();

    std::atomic<bool> down_{false};
    std::atomic<int>  x_{0}, y_{0};
    std::atomic<bool>     pen_irq_armed_{false};
    std::atomic<uint16_t> irq_status_{0};
};
