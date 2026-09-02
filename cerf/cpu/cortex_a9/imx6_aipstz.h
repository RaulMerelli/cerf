#pragma once
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

template <uint32_t kBase> class Imx6Aipstz : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().RegisterResettable(this); }
    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }
    uint8_t ReadByte(uint32_t address) override {
        return static_cast<uint8_t>(ReadWord(address & ~3u) >> ((address & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t address) override {
        return static_cast<uint16_t>(ReadWord(address & ~3u) >> ((address & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t address) override {
        const uint32_t offset = address - kBase;
        if (offset < sizeof(regs_) && (offset & 3u) == 0u) return regs_[offset >> 2];
        HaltUnsupportedAccess("read32", address, 0);
    }
    void WriteByte(uint32_t address, uint8_t value) override { MergeWrite(address, value, 1u); }
    void WriteHalf(uint32_t address, uint16_t value) override { MergeWrite(address, value, 2u); }
    void WriteWord(uint32_t address, uint32_t value) override {
        const uint32_t offset = address - kBase;
        if (offset < sizeof(regs_) && (offset & 3u) == 0u) {
            regs_[offset >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", address, value);
    }
    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    void MergeWrite(uint32_t address, uint32_t value, uint32_t width) {
        const uint32_t aligned = address & ~3u;
        const uint32_t shift = (address & 3u) * 8u;
        const uint32_t mask = (width == 1u ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }
    uint32_t regs_[0x4000u / 4u]{};
};
