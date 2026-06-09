#pragma once

#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/ps2_mouse/ps2_mouse.h"

#include <cstdint>
#include <vector>

/* HP Jornada 820 companion ASIC, nCS3 PA 0x18000000 (uncached VA 0xA4000000).
   Control regs storage-backed; the GlidePad PS/2 controller is at offset
   0x1A0000 — 8042 status +0x400, command/data +0x800 (glidepad.dll). */
class Jornada820CompanionAsic : public Peripheral {
public:
    explicit Jornada820CompanionAsic(CerfEmulator& emu)
        : Peripheral(emu), mouse_([this] { RaiseIrq(); }) {}

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x18000000u; }
    uint32_t MmioSize() const override { return 0x00400000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  v) override;
    void     WriteHalf(uint32_t addr, uint16_t v) override;
    void     WriteWord(uint32_t addr, uint32_t v) override;

    void QueuePs2Motion(int dx, int dy, uint32_t button_mask) {
        mouse_.QueueMotion(dx, dy, button_mask);
    }

private:
    static constexpr uint32_t kPs2Status = 0x1A0400u;
    static constexpr uint32_t kPs2Data   = 0x1A0800u;

    void RaiseIrq();
    uint8_t Ps2StatusByte() { return 0x80u | (mouse_.HasData() ? 0x20u : 0u); }

    std::vector<uint8_t> store_;
    Ps2Mouse             mouse_;
};
