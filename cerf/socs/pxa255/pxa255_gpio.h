#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

/* PXA255 GPIO, 3 banks x 32 pins (§4.1.3, base 0x40E00000, Table 4-49).
   A GRER/GFER-enabled edge latches GEDR (§4.1.3.5), which drives the INTC as
   a LEVEL via Intc::SetSourceLevel (cleared only by guest GEDR W1C) — an
   edge/AssertSource model would desync the INTC line from GEDR. */
class Pxa255Gpio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool     ShouldRegister() override;
    void     OnReady() override;
    uint32_t MmioBase() const override { return 0x40E00000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* Drive an external input pin's level (board wiring). Reflected by GPLR
       for that pin while it is configured as an input (GPDR bit = 0), and
       latches a GEDR edge if the transition matches GRER/GFER. May be called
       from a host input thread, so all GPIO state is guarded by mtx_. */
    void SetInputLevel(uint32_t gpio, bool high);

private:
    mutable std::mutex mtx_;

    uint32_t in_[3]   = {};   /* externally driven input levels (board wiring). */
    uint32_t out_[3]  = {};   /* output-data: GPSR sets, GPCR clears. */
    uint32_t gpdr_[3] = {};   /* pin direction (1 = output). */
    uint32_t grer_[3] = {};   /* rising-edge detect enable. */
    uint32_t gfer_[3] = {};   /* falling-edge detect enable. */
    uint32_t gedr_[3] = {};   /* edge detect status (W1C, §4.1.3.5). */
    uint32_t gafr_[6] = {};   /* alternate-function select (0L/0U..2L/2U). */

    /* Resolved pin level for a bank (§4.1.3.1): output latch for output pins,
       external input level for input pins. */
    uint32_t PinLevelLocked(uint32_t bank) const {
        return (out_[bank] & gpdr_[bank]) | (in_[bank] & ~gpdr_[bank]);
    }
    /* After a level-changing mutation, latch GEDR for transitions enabled in
       GRER/GFER and re-push the INTC level. `before` = pre-mutation levels. */
    void ApplyEdgesLocked(const uint32_t before[3]);
    /* Push the GEDR latch into Pxa255Intc bits 8/9/10 (Table 4-35). */
    void UpdateIntcLocked();
};
