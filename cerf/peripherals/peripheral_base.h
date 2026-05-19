#pragma once

#include "../core/service.h"

#include <cstdint>

class Peripheral : public Service {
public:
    using Service::Service;
    ~Peripheral() override = default;

    /* MMIO range. Stable for the lifetime of the peripheral. Both
       must be set before OnReady runs, since OnReady is where
       Register fires. */
    virtual uint32_t MmioBase() const = 0;
    virtual uint32_t MmioSize() const = 0;

    /* Read/write paths. addr is already inside [MmioBase, MmioBase + MmioSize).
       Defaults halt — peripherals override per width. */
    virtual uint8_t  ReadByte (uint32_t addr);
    virtual uint16_t ReadHalf (uint32_t addr);
    virtual uint32_t ReadWord (uint32_t addr);
    virtual uint64_t ReadDword(uint32_t addr);
    virtual void     WriteByte (uint32_t addr, uint8_t  value);
    virtual void     WriteHalf (uint32_t addr, uint16_t value);
    virtual void     WriteWord (uint32_t addr, uint32_t value);
    virtual void     WriteDword(uint32_t addr, uint64_t value);

protected:
    /* Convenience for concrete peripherals to halt their own paths
       (e.g. valid width but invalid offset within the range). Logs
       the peripheral's class name + address + value, then exits. */
    [[noreturn]] void HaltUnsupportedAccess(const char* op,
                                            uint32_t addr,
                                            uint64_t value) const;
};
