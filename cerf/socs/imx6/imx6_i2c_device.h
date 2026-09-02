#pragma once

#include <cstdint>

#include "../../core/service.h"
#include "../../state/state_stream.h"

/* An off-chip slave on an i.MX6 I2C bus. The Imx6I2c controller emulates the
   bus and the master state machine; it delegates the byte-level exchange for
   the addressed slave to the attached device. Concretes live in the board /
   off-chip-part trees and self-register with Imx6I2cBus. */
class Imx6I2cDevice : public Service {
public:
    using Service::Service;

    /* Controller MMIO base this slave sits on (e.g. 0x021A8000 = I2C3). */
    virtual uint32_t I2cControllerBase() const = 0;
    /* 7-bit slave address. */
    virtual uint8_t SlaveAddress() const = 0;

    /* A master transfer addressed to this slave has begun; read == true for a
       read transfer. Resets the slave's per-transfer state (register pointer /
       command latch). */
    virtual void StartTransfer(bool read) = 0;
    /* Master writes one byte to the slave (register pointer / command / data). */
    virtual void WriteByte(uint8_t value) = 0;
    /* Master reads one byte from the slave. */
    virtual uint8_t ReadByte() = 0;

    /* A slave whose just-written byte needs a second synthetic byte-completion
       returns true once, then clears the pending state. Default: none. */
    virtual bool TakePendingCompletion() { return false; }

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
};
