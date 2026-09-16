#pragma once
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_mmio_lane.h"

template <uint32_t kBase> class Imx6Aipstz : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        ResetRegisters();
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }
    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }
    uint8_t ReadByte(uint32_t address) override {
        return Imx6ReadMmioByte(address, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t address) override {
        return Imx6ReadMmioHalf(address, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t address) override {
        const uint32_t offset = address - kBase;
        const uint32_t index = RegisterIndex(offset);
        if (index < kRegisterCount) return regs_[index];
        HaltUnsupportedAccess("read32", address, 0);
    }
    void WriteByte(uint32_t address, uint8_t value) override { Imx6MergeMmioWrite(*this, address, value, 1u); }
    void WriteHalf(uint32_t address, uint16_t value) override { Imx6MergeMmioWrite(*this, address, value, 2u); }
    void WriteWord(uint32_t address, uint32_t value) override {
        const uint32_t offset = address - kBase;
        const uint32_t index = RegisterIndex(offset);
        if (index < kRegisterCount) {
            regs_[index] = value;
            return;
        }
        HaltUnsupportedAccess("write32", address, value);
    }
    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    static constexpr uint32_t kRegisterCount = 7u;

    /* IMX6SDLRM Rev.4 section 13.7; U-Boot a44f46af,
       arch/arm/include/asm/arch-mx6/imx-regs.h:aipstz_regs. */
    static uint32_t RegisterIndex(uint32_t offset) {
        if (offset == 0u) return 0u;
        if (offset == 4u) return 1u;
        if (offset >= 0x40u && offset <= 0x50u && (offset & 3u) == 0u)
            return 2u + (offset - 0x40u) / 4u;
        return kRegisterCount;
    }

    void ResetRegisters() {
        regs_[0] = 0x77000000u;
        regs_[1] = 0u;
        for (uint32_t i = 2u; i < kRegisterCount; ++i)
            regs_[i] = 0x44444444u;
    }

    uint32_t regs_[kRegisterCount]{};
};
