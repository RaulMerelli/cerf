#pragma once

#include "../../core/service.h"

#include <cstdint>

/* The analog and I/O pins a board hangs off its UCB1x00: the codec converts
   whichever channel ADC_CR selects, and only the board knows what is wired to
   it. ADC_CR INP[4:2] selects TSPX(0) TSMX(1) TSPY(2) TSMY(3) AD0(4) AD1(5)
   AD2(6) AD3(7) (Linux ucb1x00.h). */
class Ucb1x00Board : public Service {
public:
    using Service::Service;

    /* 10-bit conversion of auxiliary channel AD0..AD3 (INP 4..7). */
    virtual uint16_t AuxAdc(uint8_t channel) = 0;

    /* Touch plate. X and Y are 10-bit raw ADC counts, not pixels. */
    virtual bool     TouchDown() const = 0;
    virtual uint16_t TouchAdcX() = 0;
    virtual uint16_t TouchAdcY() = 0;
    virtual uint16_t TouchAdcPressure() = 0;

    /* IO_DATA[15:0]. `driven` is what the guest last wrote, so a board returns
       its input lines merged over the bits it does not drive. */
    virtual uint16_t IoData(uint16_t driven) = 0;

    /* Pen-down interrupt, reported through IE_STATUS and armed via IE_FAL; the armed
       IE_FAL pen-detect bits are passed so a board latches exactly the armed bit. */
    virtual uint16_t PenIrqStatus() = 0;
    virtual void     ClearPenIrq(uint16_t mask) = 0;
    virtual void     SetPenIrqArmed(uint16_t armed_bits) = 0;
};
