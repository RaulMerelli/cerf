#pragma once

#include "../../core/service.h"

#include <cstdint>

class StateWriter;
class StateReader;

class Vrc5477Intc : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;

    /* off is relative to the INTC block base (0x400). 32-bit registers. */
    uint32_t ReadReg(uint32_t off);
    void     WriteReg(uint32_t off, uint32_t value);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    /* INTnSTAT: the pending IRQs that are enabled and routed to CPU output n
       (4-bit field per IRQ in INTCTRL[irq/8]: bit3 = enable, bits[2:0] = route). */
    uint32_t StatusForLine(uint32_t n) const;

    uint32_t intctrl_[4] = {0, 0, 0, 0};
    uint32_t intppes0_   = 0;
    uint32_t intppes1_   = 0;
    uint32_t cpustat_    = 0;
    uint32_t busctrl_    = 0;
    uint32_t nmistat_    = 0;
    uint32_t pending_    = 0;   /* latched IRQ requests (set by sources, W1C via INTCLR32) */
};
