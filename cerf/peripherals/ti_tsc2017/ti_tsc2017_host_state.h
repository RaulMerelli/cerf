#pragma once

#include <cstdint>
#include <mutex>

#include "../../core/service.h"

/* TI TSC2017 host-side sample state. The board supplies already-calibrated raw
   ADC coordinates via SetPen. */
class Tsc2017HostState : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;

    struct Sample {
        /* On release X/Y keep the last valid conversion; touch.dll reads them
           alongside the zero-pressure sample. Overwriting with the open-circuit
           ADC value would place the pen-up at a screen corner. */
        bool     down = false;
        bool     penirq_low = false;
        uint16_t x = 0x800u;
        uint16_t y = 0x800u;
        uint16_t z1 = 0x000u;
        uint16_t z2 = 0x000u;
    };

    using IrqChangedCallback = void (*)(void*);

    void   SetPen(bool down, uint16_t raw_x, uint16_t raw_y);
    Sample Get();
    bool   PenIrqPending();
    void   ClearPenIrqPending();
    void   SetIrqChangedCallback(IrqChangedCallback cb, void* ctx);
    bool   PenIrqLineHigh();

private:
    void NotifyIrqChanged();

    std::mutex         mutex_;
    Sample             state_;
    bool               penirq_pending_ = false;
    IrqChangedCallback irq_cb_ = nullptr;
    void*              irq_ctx_ = nullptr;
};
