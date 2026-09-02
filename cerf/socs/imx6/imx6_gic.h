#pragma once

#include "../../core/service.h"

#include <cstdint>

class StateReader;
class StateWriter;

class Imx6Gic : public Service {
public:
    using Service::Service;

    /* spi is 0-based (GIC ID = spi + 32). Thread-safe. */
    virtual void AssertSpi(int spi) = 0;
    virtual void DeAssertSpi(int spi) = 0;

    static constexpr uint32_t kMmioBase = 0x00A00000u;
    static constexpr uint32_t kMmioSize = 0x2000u;

    virtual uint32_t ReadMmio(uint32_t offset) = 0;
    virtual void WriteMmio(uint32_t offset, uint32_t value) = 0;
    virtual void SaveGicState(StateWriter& writer) = 0;
    virtual void RestoreGicState(StateReader& reader) = 0;
    virtual void PostRestoreGicState() = 0;
    virtual bool Tick() = 0;
};
