#pragma once

#include "../peripheral_base.h"

#include <cstdint>

/* SA-1111 System Controller (Developer's Manual ch.5, base 0x40000200):
   SKPCR +0x00, SKCDR +0x04, SKAUD +0x08, then PWM control through +0x20.
   All reset 0; CERF gates no clocks, registers store. */
class Sa1111SystemController : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x40000200u; }
    uint32_t MmioSize() const override { return 0x00000200u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* §5.2.3: SKAUD bits 6:0 hold (audio clock divider - 1); fs =
       143.7696 MHz / (256 * divider) reproduces Table 5-1 / Table 7-6
       (divider 25 -> 22.46 kHz actual). */
    uint32_t AudioSampleRateHz() const {
        const uint32_t divider = (regs_[2] & 0x7Fu) + 1u;
        return 143769600u / (256u * divider);
    }

private:
    uint32_t regs_[9] = {};
};
