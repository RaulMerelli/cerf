#pragma once

#include "../../core/service.h"

#include <cstdint>

class StateWriter;
class StateReader;

/* PMUTCLKDIVREG (VR4131 UM U15350EJ2V0UM 12.2.6) is the VTCLK/TCLK divider the PMU register
   and CLKSPEEDREG (BCU, 7.2.7 p142) both view; the setting "becomes valid after a reset
   other than an RTC reset occurs" (12.2.6). */
class Vr4122ClockState : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void     SetPending(uint16_t tclkdiv);
    uint16_t Pending() const { return pending_; }
    uint16_t Active()  const { return active_; }

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    uint16_t pending_ = 0;
    uint16_t active_  = 0;
};
