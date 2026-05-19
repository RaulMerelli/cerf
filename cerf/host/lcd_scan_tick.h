#pragma once

#include "../core/service.h"

/* Per-board hook for SoC display peripherals that need to advance
   scan state at panel refresh rate (VSYNC bit, GO bit auto-clear,
   FRAMEDONE pulses). HostWindow's WM_TIMER calls OnHostTick on the
   registered concrete each present cycle. */

class LcdScanTick : public Service {
public:
    using Service::Service;
    ~LcdScanTick() override = default;

    virtual void OnHostTick() = 0;
};
