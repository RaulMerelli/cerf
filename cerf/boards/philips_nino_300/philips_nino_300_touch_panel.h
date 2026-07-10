#pragma once

#include "../../core/service.h"

#include <atomic>
#include <cstdint>

/* Shared pen state for the Nino's UCB1200 resistive touch panel. A host
   TouchInput drives SetPen; PhilipsNino300UcbBoard reads it to answer
   touch.dll's ADC reads over the SIB subframe-0 codec. */
class PhilipsNino300TouchPanel : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;

    void SetPen(bool down, int x, int y);   /* asserts SIBIRQPOSINT on a down edge */
    bool Down() const { return down_.load(std::memory_order_acquire); }
    int  X()    const { return x_.load(std::memory_order_relaxed); }
    int  Y()    const { return y_.load(std::memory_order_relaxed); }

    /* UCB pen-detect IRQ (UCB_IE_TSMX bit 13 on the Nino), armed via IE_FAL. */
    void     SetPenIrqArmed(uint16_t bits) { pen_irq_armed_.store(bits != 0, std::memory_order_release); }
    void     ClearPenIrq(uint16_t mask);
    uint16_t PenIrqStatus() const { return irq_status_.load(std::memory_order_acquire); }

private:
    void AssertPenIrq();

    std::atomic<bool> down_{false};
    std::atomic<int>  x_{0}, y_{0};
    std::atomic<bool>     pen_irq_armed_{false};
    std::atomic<uint16_t> irq_status_{0};
};
