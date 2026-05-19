#pragma once

#include "../../core/service.h"

#include <cstdint>

class I2cSlave : public Service {
public:
    using Service::Service;
    ~I2cSlave() override = default;

    /* Return true iff this slave answers the given 7-bit I2C
       address. A single slave may answer at multiple addresses
       (the TWL4030 answers at four — one per internal module). */
    virtual bool MatchesAddress(uint8_t slave_addr) const = 0;

    /* Called at the start of every master-driven transaction
       directed at `slave_addr`. Slaves typically reset their
       sub-address-pending flag here so the next master write byte
       is treated as a new sub-address. */
    virtual void TxnStart(uint8_t slave_addr) = 0;

    /* Master is sending us one byte. First byte after TxnStart is
       conventionally the sub-address; subsequent bytes are data
       written into the register file at sub_address++. */
    virtual void TxnWriteByte(uint8_t slave_addr, uint8_t byte) = 0;

    /* Master is reading one byte from us. We return the register
       at the current sub_address and auto-increment. */
    virtual uint8_t TxnReadByte(uint8_t slave_addr) = 0;
};
