#pragma once

#include <cstdint>

#include "../../core/service.h"
#include "../../state/state_stream.h"

class Imx6I2cDevice : public Service {
public:
    using Service::Service;

    virtual uint32_t I2cControllerBase() const = 0;
    virtual uint8_t SlaveAddress() const = 0;

    virtual void StartTransfer(bool read) = 0;
    virtual void WriteByte(uint8_t value) = 0;
    virtual uint8_t ReadByte() = 0;

    virtual bool TakePendingCompletion() { return false; }

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
};
