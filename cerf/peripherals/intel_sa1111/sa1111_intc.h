#pragma once

#include "../peripheral_base.h"

#include <cstdint>

/* SA-1111 Interrupt Controller (Developer's Manual Table 11-2, base
   0x40001600). Sub-blocks call RaiseInterrupt/LowerInterrupt with their
   Table 11-1 source number (0..63); the chip IRQ output cascades through
   an SA-1110 GPIO. */
class Sa1111Intc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x40001600u; }
    uint32_t MmioSize() const override { return 0x00000200u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* Source 0..31 → bank 0, 32..63 → bank 1. Raise latches a pending
       status bit; Lower clears it (for level-style sources whose driver
       owns deassert). Edge sources leave the bit for software W1C. */
    void RaiseInterrupt(uint8_t source);
    void LowerInterrupt(uint8_t source);

    /* True while any enabled source is pending — the level the chip's
       cascade output drives onto its SA-1110 GPIO. */
    bool OutputAsserted() const { return (status0_ & enable0_) ||
                                         (status1_ & enable1_); }

private:
    void DriveCascadeOutput(bool pulse_low_first);
    uint32_t inttest0_ = 0, inttest1_ = 0;
    uint32_t enable0_  = 0, enable1_  = 0;   /* INTEN0/1 */
    uint32_t polarity0_ = 0, polarity1_ = 0; /* INTPOL0/1 */
    uint32_t tstsel_   = 0;                   /* INTTSTSEL */
    uint32_t status0_  = 0, status1_  = 0;   /* INTSTATCLR0/1 */
    uint32_t wake_en0_ = 0, wake_en1_ = 0;
    uint32_t wake_pol0_ = 0, wake_pol1_ = 0;
};
