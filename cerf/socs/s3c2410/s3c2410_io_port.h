#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../host/guest_deep_sleep.h"

#include <cstdint>
#include <mutex>

class S3C2410IoPort : public Peripheral, public DeepSleepWaker {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    /* DeepSleepWaker: GSTATUS2 (+0xB4) bit1 = wakeup-from-PowerOff. */
    void LatchSleepWakeCause() override;

    uint32_t MmioBase() const override { return 0x56000000u; }
    uint32_t MmioSize() const override { return 0x00100000u; }  /* 1 MB section */

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void AssertEint(int n);
    void ClearEint (int n);

private:
    static constexpr size_t   kSlotCount    = 48;
    static constexpr uint32_t kSlotEintMask = 0xA4u / 4u;
    static constexpr uint32_t kSlotEintPend = 0xA8u / 4u;

    /* EXTINTn trigger type nibble for EINTn (0 low-level, 1 high-level,
       2-3 falling, 4-5 rising, 6-7 both edges). Caller holds
       state_mutex_. */
    int ExtintTypeLocked(int n) const;

    /* Re-latch still-asserted level lines into EINTPEND and return the
       SRCPND rollup bits for everything pending + unmasked. Caller
       holds state_mutex_; the returned bits are AssertIrq'd after the
       lock drops. */
    uint32_t ReevaluateLocked();

    mutable std::mutex state_mutex_;
    /* Word-aligned register block. The remaining (0x100000 - 0xC0) of
       the 1 MB section is unused; access there halts via the slot
       bound check. Power-on reset values of EINTMASK / EINTPEND are
       documented as 0 per S3C2410 UM § 9 — matching the default. */
    uint32_t storage_[kSlotCount] = {};

    /* External lines currently held asserted by peripherals
       (level-triggered sources). EINTPEND re-latches from this on
       every guest EINTPEND/EINTMASK write — a level line that stays
       high keeps interrupting until the peripheral drops it. */
    uint32_t eint_level_ = 0u;
};
