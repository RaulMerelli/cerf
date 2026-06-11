#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

/* Logged scaffold (NOT a device model) for an NEC P530 board-device MMIO window
   on a static chip-select: stores writes, returns stored/0 on reads, and logs
   unmodeled accesses. A concrete supplies MmioBase() (the OAT PA) + WindowName(). */
class NecMobilePro900BoardWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    /* OAT board-device bands are 1 MB each. A concrete with a different span
       overrides this. */
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    /* Short identifier for this window in the unmodeled-access logs. */
    virtual const char* WindowName() const = 0;

private:
    uint32_t ReadReg(uint32_t addr);

    /* First access to each PA logs once (production-visible Caution), then
       stays quiet — surfaces the unmodeled board-device registers in field
       logs without flooding on polled access. */
    bool FirstTouch(uint32_t addr) { return logged_.insert(addr).second; }

    std::unordered_map<uint32_t, uint32_t> regs_;
    std::unordered_set<uint32_t> logged_;
};
