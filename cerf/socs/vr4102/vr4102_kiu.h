#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

/* NEC VR4102 KIU (Keyboard Interface Unit), 96-key matrix scanner (UM ch.21). */
class Vr4102Kiu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x0B000180u; }
    uint32_t MmioSize() const override { return 0x20u; }

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("KIU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("KIU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("KIU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("KIU WriteWord", addr, v); }

    /* matrix_index = 16*KIUDATn + bit (0..95): keybddr's scan-index encoding
       (sub_15B4848 @ 0x15B0978). */
    void SetKeyState(uint8_t matrix_index, bool pressed);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    mutable std::mutex mtx_;

    uint16_t matrix_[6] = {0, 0, 0, 0, 0, 0};   /* KIUDAT0-5: bit set = key pressed. */
    uint16_t scanrep_   = 0;
    uint16_t causes_    = 0;

    bool EnabledLocked() const;
    void PublishCausesLocked();
};
