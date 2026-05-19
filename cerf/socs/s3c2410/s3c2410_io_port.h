#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

class S3C2410IoPort : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x56000000u; }
    uint32_t MmioSize() const override { return 0x00100000u; }  /* 1 MB section */

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void AssertEint(int n);
    void ClearEint (int n);

private:
    static constexpr size_t   kSlotCount    = 48;
    static constexpr uint32_t kSlotEintMask = 0xA4u / 4u;
    static constexpr uint32_t kSlotEintPend = 0xA8u / 4u;

    mutable std::mutex state_mutex_;
    /* Word-aligned register block. The remaining (0x100000 - 0xC0) of
       the 1 MB section is unused; access there halts via the slot
       bound check. Power-on reset values of EINTMASK / EINTPEND are
       documented as 0 per S3C2410 UM § 9 — matching the default. */
    uint32_t storage_[kSlotCount] = {};
};
