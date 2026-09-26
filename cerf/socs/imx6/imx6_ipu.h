#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <vector>

class Imx6Ipu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint8_t ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;
    void WriteByte(uint32_t addr, uint8_t value) override;
    void WriteHalf(uint32_t addr, uint16_t value) override;
    void WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void AdvanceScanTick();

private:
    void WriteMerged(uint32_t off, uint32_t value);
    void EnsureModelledRead(const char* what, uint32_t addr, uint32_t ipu_off) const;
    void EnsureModelledWrite(const char* what, uint32_t addr, uint32_t ipu_off, uint32_t value) const;
    void ResetDisplayBlockDefaults();
    void RaiseIpuIrq(uint32_t irq);
    void RaiseDisplayFrameEvents(uint32_t channel_mask, bool high = false);
    void UpdateInterruptLines();
    uint32_t EnabledMaskForChannel(uint32_t ch) const;
    uint32_t ReadyMaskForChannel(uint32_t ch) const;
    void UpdateCpmemCurrentBuffer(uint32_t ipu_off, uint32_t mask, uint32_t buffer);
    void MaybeSignalDisplay();

    std::vector<uint32_t> regs_;
    bool sync_irq_asserted_ = false;
    bool err_irq_asserted_ = false;
};
