#pragma once

#include "../../core/service.h"

#include <cstdint>

class Pd6710Card : public Service {
public:
    using Service::Service;
    ~Pd6710Card() override = default;

    virtual void PowerOn()  = 0;
    virtual void PowerOff() = 0;

    /* I/O space — the card's register file. Offsets 0..N relative to
       whichever PCMCIA I/O window the driver configured to map this
       card. NE2000 register file is 0..0x1F. */
    virtual uint8_t  ReadByte  (uint32_t offset)                       = 0;
    virtual uint16_t ReadHalf  (uint32_t offset)                       = 0;
    virtual void     WriteByte (uint32_t offset, uint8_t  value)       = 0;
    virtual void     WriteHalf (uint32_t offset, uint16_t value)       = 0;

    /* Memory space — used for the card's CIS, FCSR, and on-card RAM.
       Offsets are direct card-space addresses (not translated through
       any window the way I/O is). */
    virtual uint8_t  ReadMemoryByte  (uint32_t offset)                 = 0;
    virtual uint16_t ReadMemoryHalf  (uint32_t offset)                 = 0;
    virtual void     WriteMemoryByte (uint32_t offset, uint8_t  value) = 0;
    virtual void     WriteMemoryHalf (uint32_t offset, uint16_t value) = 0;
};
