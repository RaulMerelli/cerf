#pragma once

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/imx6/imx6_gic.h"
#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"
#include "../../socs/freescale_sdma_impl.h"
#include "../../state/state_stream.h"

namespace {

/* IMX6SDLRM Rev.4 section 29.5: GPIOx_DR through GPIOx_EDGE_SEL. */
template <uint32_t kBase> class Imx6Gpio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
        emu_.Get<Imx6GpioBus>().RegisterBank(MmioBase(), [this] { UpdateIrq(); });
        UpdateIrq();
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off <= kEdgeSel && (off & 3u) == 0) {
            return ReadRegister(off);
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { MergeWrite(addr, value, 1); }
    void WriteHalf(uint32_t addr, uint16_t value) override { MergeWrite(addr, value, 2); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off <= kEdgeSel && (off & 3u) == 0) {
            WriteRegister(off, value);
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes(regs_, sizeof(regs_));
        w.Write(input_level_);
        emu_.Get<Imx6GpioBus>().SaveSources(MmioBase(), w);
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes(regs_, sizeof(regs_));
        r.Read(input_level_);
        emu_.Get<Imx6GpioBus>().RestoreSources(MmioBase(), r);
        reset_restore_pending_ = false;
    }
    void SaveResetState(StateWriter& w) override {
        w.WriteBytes(regs_, sizeof(regs_));
        w.Write(input_level_);
    }
    void RestoreResetState(StateReader& r) override {
        r.ReadBytes(regs_, sizeof(regs_));
        r.Read(input_level_);
        reset_restore_pending_ = true;
    }
    void PostRestore() override {
        if (reset_restore_pending_) return;
        if (Imx6GpioInputSource* s = Source())
            s->OnEffectiveOutputs(regs_[kDr >> 2], regs_[kGdir >> 2]);
        emu_.Get<Imx6GpioBus>().PostRestoreSources(MmioBase());
        UpdateIrq();
    }
    void PostReset(ResetLineKind kind) override {
        if (Imx6GpioInputSource* s = Source()) {
            s->OnControllerReset(kind);
            s->OnEffectiveOutputs(regs_[kDr >> 2], regs_[kGdir >> 2]);
        }
        emu_.Get<Imx6GpioBus>().PostRestoreSources(MmioBase());
        reset_restore_pending_ = false;
        UpdateIrq();
    }

private:
    static constexpr uint32_t kDr = 0x00u;
    static constexpr uint32_t kGdir = 0x04u;
    static constexpr uint32_t kPsr = 0x08u;
    static constexpr uint32_t kIcr1 = 0x0Cu;
    static constexpr uint32_t kIcr2 = 0x10u;
    static constexpr uint32_t kImr = 0x14u;
    static constexpr uint32_t kIsr = 0x18u;
    static constexpr uint32_t kEdgeSel = 0x1Cu;

    uint32_t ReadRegister(uint32_t off) {
        switch (off) {
        case kDr: return ReadDataRegister();
        case kGdir: return regs_[kGdir >> 2];
        case kPsr: return ReadPadStatus();
        case kIcr1:
        case kIcr2:
        case kImr:
        case kEdgeSel: return regs_[off >> 2];
        case kIsr: UpdateLevelSensitiveStatus(); return regs_[kIsr >> 2];
        default: HaltUnsupportedAccess("imx6-gpio read32 unmodelled register", MmioBase() + off, 0);
        }
    }

    uint32_t ReadPadStatus() const {
        /* GPIO_PSR is the output latch for output pins and the sampled pad level
           for inputs; a board source drives its own input pins. */
        uint32_t inputs = input_level_;
        if (Imx6GpioInputSource* s = Source()) inputs = s->ApplyPadInputs(inputs);
        return (regs_[kDr >> 2] & regs_[kGdir >> 2]) | (inputs & ~regs_[kGdir >> 2]);
    }

    void WriteRegister(uint32_t off, uint32_t value) {
        switch (off) {
        case kPsr:
            /* GPIO_PSR is read-only in i.MX6. */
            return;
        case kIsr:
            /* GPIO_ISR is write-one-to-clear. */
            if (Imx6GpioInputSource* s = Source()) s->OnIsrClear(value);
            regs_[kIsr >> 2] &= ~value;
            UpdateIrq();
            return;
        case kImr:
            regs_[kImr >> 2] = value;
            UpdateIrq();
            return;
        case kIcr1:
        case kIcr2:
        case kEdgeSel:
            regs_[off >> 2] = value;
            UpdateLevelSensitiveStatus();
            UpdateIrq();
            return;
        case kDr:
        case kGdir:
            regs_[off >> 2] = value;
            if (Imx6GpioInputSource* s = Source())
                s->OnEffectiveOutputs(regs_[kDr >> 2], regs_[kGdir >> 2]);
            UpdateLevelSensitiveStatus();
            UpdateIrq();
            return;
        default: HaltUnsupportedAccess("imx6-gpio write32 unmodelled register", MmioBase() + off, value);
        }
    }

    uint32_t ReadDataRegister() {
        uint32_t value = regs_[kDr >> 2];
        if (Imx6GpioInputSource* s = Source()) value = s->ApplyDataRead(value);
        return value;
    }

    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    uint32_t BankIndex() const { return (MmioBase() - 0x0209C000u) / 0x4000u; }

    uint32_t GpioSpiLow16() const { return 66u + BankIndex() * 2u; }

    Imx6GpioInputSource* Source() const { return emu_.Get<Imx6GpioBus>().Find(MmioBase()); }

    uint32_t InterruptSenseForPin(uint32_t pin) const {
        const uint32_t reg = pin < 16u ? regs_[kIcr1 >> 2] : regs_[kIcr2 >> 2];
        return (reg >> ((pin & 15u) * 2u)) & 3u;
    }

    void UpdateLevelSensitiveStatus() {
        const uint32_t level = ReadPadStatus();
        uint32_t level_mask = 0;
        uint32_t active_mask = 0;
        for (uint32_t pin = 0; pin < 32u; ++pin) {
            const uint32_t bit = 1u << pin;
            if (regs_[kEdgeSel >> 2] & bit) continue;
            const uint32_t sense = InterruptSenseForPin(pin);
            if (sense > 1u) continue; /* 10/11 are edge modes unless EDGE_SEL forces both. */
            level_mask |= bit;
            const bool high = (level & bit) != 0;
            if ((sense == 0u && !high) || (sense == 1u && high)) active_mask |= bit;
        }
        regs_[kIsr >> 2] = (regs_[kIsr >> 2] & ~level_mask) | active_mask;
        if (Imx6GpioInputSource* s = Source()) regs_[kIsr >> 2] |= s->PendingIsr();
    }

    void UpdateIrq() {
        UpdateLevelSensitiveStatus();
        auto* gic = emu_.TryGet<Imx6Gic>();
        if (!gic) return;

        const uint32_t pending = regs_[kIsr >> 2] & regs_[kImr >> 2];
        if (pending & 0x0000FFFFu)
            gic->AssertSpi(static_cast<int>(GpioSpiLow16()));
        else
            gic->DeAssertSpi(static_cast<int>(GpioSpiLow16()));
        if (pending & 0xFFFF0000u)
            gic->AssertSpi(static_cast<int>(GpioSpiLow16() + 1u));
        else
            gic->DeAssertSpi(static_cast<int>(GpioSpiLow16() + 1u));
    }

    uint32_t regs_[0x4000u / 4u]{};
    uint32_t input_level_ = 0xFFFFFFFFu;
    bool reset_restore_pending_ = false;

};

} /* namespace */
