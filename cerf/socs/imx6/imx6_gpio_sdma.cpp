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

/* GPIO1..GPIO7. Early OEMInit clears interrupt/status words across all
   banks before the GPIO driver takes over. */
class Imx6Gpio1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<Imx6GpioBus>().RegisterBank(MmioBase(), [this] { UpdateIrq(); });
        UpdateIrq();
    }

    uint32_t MmioBase() const override { return 0x0209C000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(
            ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(
            ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0) {
            return ReadRegister(off);
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        MergeWrite(addr, value, 1);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        MergeWrite(addr, value, 2);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0) {
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
        UpdateIrq();
    }

private:
    static constexpr uint32_t kDr      = 0x00u;
    static constexpr uint32_t kGdir    = 0x04u;
    static constexpr uint32_t kPsr     = 0x08u;
    static constexpr uint32_t kIcr1    = 0x0Cu;
    static constexpr uint32_t kIcr2    = 0x10u;
    static constexpr uint32_t kImr     = 0x14u;
    static constexpr uint32_t kIsr     = 0x18u;
    static constexpr uint32_t kEdgeSel = 0x1Cu;

    uint32_t ReadRegister(uint32_t off) {
        switch (off) {
        case kDr:
            return ReadDataRegister();
        case kGdir:
            return regs_[kGdir >> 2];
        case kPsr:
            return ReadPadStatus();
        case kIcr1:
        case kIcr2:
        case kImr:
        case kEdgeSel:
            return regs_[off >> 2];
        case kIsr:
            UpdateLevelSensitiveStatus();
            return regs_[kIsr >> 2];
        default:
            return regs_[off >> 2];
        }
    }

    uint32_t ReadPadStatus() const {
        /* GPIO_PSR is the output latch for output pins and the sampled pad level
           for inputs; a board source drives its own input pins. */
        uint32_t inputs = input_level_;
        if (Imx6GpioInputSource* s = Source())
            inputs = s->ApplyPadInputs(inputs);
        return (regs_[kDr >> 2] & regs_[kGdir >> 2])
             | (inputs & ~regs_[kGdir >> 2]);
    }

    void WriteRegister(uint32_t off, uint32_t value) {
        switch (off) {
        case kPsr:
            /* GPIO_PSR is read-only in i.MX6. */
            return;
        case kIsr:
            /* GPIO_ISR is write-one-to-clear. */
            if (Imx6GpioInputSource* s = Source())
                s->OnIsrClear(value);
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
            UpdateLevelSensitiveStatus();
            UpdateIrq();
            return;
        default:
            regs_[off >> 2] = value;
            return;
        }
    }

    uint32_t ReadDataRegister() {
        uint32_t value = regs_[kDr >> 2];
        if (Imx6GpioInputSource* s = Source())
            value = s->ApplyDataRead(value);
        return value;
    }

    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask =
            (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned,
            (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    uint32_t BankIndex() const {
        return (MmioBase() - 0x0209C000u) / 0x4000u;
    }

    uint32_t GpioSpiLow16() const {
        return 66u + BankIndex() * 2u;
    }

    Imx6GpioInputSource* Source() const {
        return emu_.Get<Imx6GpioBus>().Find(MmioBase());
    }

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
            if (regs_[kEdgeSel >> 2] & bit)
                continue;
            const uint32_t sense = InterruptSenseForPin(pin);
            if (sense > 1u)
                continue;       /* 10/11 are edge modes unless EDGE_SEL forces both. */
            level_mask |= bit;
            const bool high = (level & bit) != 0;
            if ((sense == 0u && !high) || (sense == 1u && high))
                active_mask |= bit;
        }
        regs_[kIsr >> 2] = (regs_[kIsr >> 2] & ~level_mask) | active_mask;
        if (Imx6GpioInputSource* s = Source())
            regs_[kIsr >> 2] |= s->PendingIsr();
    }

    void UpdateIrq() {
        UpdateLevelSensitiveStatus();
        auto* gic = emu_.TryGet<Imx6Gic>();
        if (!gic) return;

        const uint32_t pending = regs_[kIsr >> 2] & regs_[kImr >> 2];
        if (pending & 0x0000FFFFu) gic->AssertSpi(static_cast<int>(GpioSpiLow16()));
        else                       gic->DeAssertSpi(static_cast<int>(GpioSpiLow16()));
        if (pending & 0xFFFF0000u) gic->AssertSpi(static_cast<int>(GpioSpiLow16() + 1u));
        else                       gic->DeAssertSpi(static_cast<int>(GpioSpiLow16() + 1u));
    }

    uint32_t regs_[0x4000u / 4u]{};
    uint32_t input_level_ = 0xFFFFFFFFu;
};

class Imx6Gpio2 : public Imx6Gpio1 {
public:
    using Imx6Gpio1::Imx6Gpio1;
    uint32_t MmioBase() const override { return 0x020A0000u; }
};
class Imx6Gpio3 : public Imx6Gpio1 {
public:
    using Imx6Gpio1::Imx6Gpio1;
    uint32_t MmioBase() const override { return 0x020A4000u; }
};
class Imx6Gpio4 : public Imx6Gpio1 {
public:
    using Imx6Gpio1::Imx6Gpio1;
    uint32_t MmioBase() const override { return 0x020A8000u; }
};
class Imx6Gpio5 : public Imx6Gpio1 {
public:
    using Imx6Gpio1::Imx6Gpio1;
    uint32_t MmioBase() const override { return 0x020AC000u; }
};
class Imx6Gpio6 : public Imx6Gpio1 {
public:
    using Imx6Gpio1::Imx6Gpio1;
    uint32_t MmioBase() const override { return 0x020B0000u; }
};
class Imx6Gpio7 : public Imx6Gpio1 {
public:
    using Imx6Gpio1::Imx6Gpio1;
    uint32_t MmioBase() const override { return 0x020B4000u; }
};

/* i.MX6 Keypad Port (KPP), base 0x020B8000.  Linux imx6qdl.dtsi exposes this
   as compatible "fsl,imx6q-kpp", "fsl,imx21-kpp", SPI 82.  The register block
   is the same 16-bit KPCR/KPSR/KDDR/KPDR cell already used by i.MX31/i.MX51:
   model only the documented registers and fail loud on anything else. */
class Imx6Kpp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        kpsr_ = kKpsrReset;
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x020B8000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint16_t v = ReadHalf(addr & ~1u);
        return static_cast<uint8_t>((addr & 1u) ? (v >> 8) : v);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        uint16_t value = 0;
        switch (off) {
        case kKpcr:
            value = kpcr_;
            break;
        case kKpsr:
            value = kpsr_ & (kKpkd | kKpkr | kKdie | kKrie | kKdsr);
            break;
        case kKddr:
            value = kddr_;
            break;
        case kKpdr:
            value = ReadKpdr();
            break;
        default:
            HaltUnsupportedAccess("imx6-kpp read16 unmodelled register", addr, 0);
        }
        return value;
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 1u) != 0u)
            HaltUnsupportedAccess("imx6-kpp read32 unaligned", addr, 0);
        return uint32_t(ReadHalf(addr)) | (uint32_t(ReadHalf(addr + 2u)) << 16);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t aligned = addr & ~1u;
        const uint16_t old = ReadHalf(aligned);
        const uint16_t merged = (addr & 1u)
            ? static_cast<uint16_t>((old & 0x00FFu) | (uint16_t(value) << 8))
            : static_cast<uint16_t>((old & 0xFF00u) | value);
        WriteHalf(aligned, merged);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case kKpcr:
            kpcr_ = value;
            return;
        case kKpsr:
            /* KPKD/KPKR are W1C status bits; KDIE/KRIE are plain enables.
               KDSC/KRSS are synchronization strobes and self-clear. */
            kpsr_ = static_cast<uint16_t>(kpsr_ & ~(value & (kKpkd | kKpkr)));
            kpsr_ = static_cast<uint16_t>((kpsr_ & ~(kKdie | kKrie))
                                        | (value & (kKdie | kKrie))
                                        | kKpsrReset);
            UpdateIrq();
            return;
        case kKddr:
            kddr_ = value;
            return;
        case kKpdr:
            kpdr_latch_ = value;
            return;
        default:
            HaltUnsupportedAccess("imx6-kpp write16 unmodelled register", addr, value);
        }
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 1u) != 0u)
            HaltUnsupportedAccess("imx6-kpp write32 unaligned", addr, value);
        WriteHalf(addr, static_cast<uint16_t>(value));
        WriteHalf(addr + 2u, static_cast<uint16_t>(value >> 16));
    }

    void SaveState(StateWriter& w) override {
        w.Write(kpcr_);
        w.Write(kpsr_);
        w.Write(kddr_);
        w.Write(kpdr_latch_);
    }
    void RestoreState(StateReader& r) override {
        r.Read(kpcr_);
        r.Read(kpsr_);
        r.Read(kddr_);
        r.Read(kpdr_latch_);
        UpdateIrq();
    }

private:
    static constexpr uint32_t kKpcr = 0x00u;
    static constexpr uint32_t kKpsr = 0x02u;
    static constexpr uint32_t kKddr = 0x04u;
    static constexpr uint32_t kKpdr = 0x06u;

    static constexpr uint16_t kKpkd = 0x0001u;
    static constexpr uint16_t kKpkr = 0x0002u;
    static constexpr uint16_t kKdie = 0x0100u;
    static constexpr uint16_t kKrie = 0x0200u;
    static constexpr uint16_t kKdsr = 0x0400u;
    static constexpr uint16_t kKpsrReset = kKdsr;

    uint16_t ReadKpdr() const {
        /* KDDR=1 pins read the driven output latch.  Input matrix lines are
           inactive high for this board until host key injection is wired. */
        return static_cast<uint16_t>((kpdr_latch_ & kddr_) | (0xFFFFu & ~kddr_));
    }

    void UpdateIrq() {
        const bool desired = ((kpsr_ & kKpkd) && (kpsr_ & kKdie))
                          || ((kpsr_ & kKpkr) && (kpsr_ & kKrie));
        auto& gic = emu_.Get<Imx6Gic>();
        if (desired) gic.AssertSpi(82);
        else         gic.DeAssertSpi(82);
    }


    uint16_t kpcr_ = 0;
    uint16_t kpsr_ = kKpsrReset;
    uint16_t kddr_ = 0;
    uint16_t kpdr_latch_ = 0xFFFFu;
};

/* i.MX6 SDMA: same Freescale SDMA IP as i.MX31/51. CHNENBL0 is at 0x200
   (not 0x80 like i.MX31) with 48 event channels: Linux drivers/dma/imx-sdma.c
   SDMA_CHNENBL0_IMX35=0x200, sdma_imx6q.num_events=48. */
class Imx6Sdma
    : public cerf_freescale_sdma_detail::FreescaleSdmaBase<0x020EC000u, SocFamily::iMX6> {
public:
    using FreescaleSdmaBase::FreescaleSdmaBase;

protected:
    /* SDMA AP interrupt = GIC SPI 2. Linux arch/arm/boot/dts/nxp/imx/imx6sl.dtsi
       sdma node: interrupts = <GIC_SPI 2 IRQ_TYPE_LEVEL_HIGH>. */
    void AssertIrqLine()   override { emu_.Get<Imx6Gic>().AssertSpi  (2); }
    void DeassertIrqLine() override { emu_.Get<Imx6Gic>().DeAssertSpi(2); }

    uint32_t ChnenblBase()  const override { return 0x200u; }
    uint32_t ChnenblCount() const override { return kChnenblCount; }

    /* i.MX6-specific divergent registers: EVT_MIRROR @0x54 (RO), CHNENBL0
       at 0x200 with 48 event-enable words (Linux imx-sdma.c sdma_imx6q). */
    bool ReadExtra(uint32_t off, uint32_t& out) override {
        if (off == 0x054u) { out = 0; return true; }
        if (off >= 0x200u && off < 0x200u + kChnenblCount * 4u
                && (off & 3u) == 0) {
            out = chnenbl_[(off - 0x200u) / 4u]; return true;
        }
        return false;
    }
    bool WriteExtra(uint32_t off, uint32_t value) override {
        if (off >= 0x200u && off < 0x200u + kChnenblCount * 4u
                && (off & 3u) == 0) {
            chnenbl_[(off - 0x200u) / 4u] = value;
            return true;
        }
        return false;
    }
    void SaveExtra(StateWriter& w) override    { w.WriteBytes(chnenbl_, sizeof(chnenbl_)); }
    void RestoreExtra(StateReader& r) override { r.ReadBytes(chnenbl_, sizeof(chnenbl_)); }
    void ResetExtra() override { for (auto& c : chnenbl_) c = 0u; }

private:
    static constexpr uint32_t kChnenblCount = 48u;
    uint32_t chnenbl_[kChnenblCount] = {};
};

}  /* namespace */

REGISTER_SERVICE(Imx6Gpio1);
REGISTER_SERVICE(Imx6Gpio2);
REGISTER_SERVICE(Imx6Gpio3);
REGISTER_SERVICE(Imx6Gpio4);
REGISTER_SERVICE(Imx6Gpio5);
REGISTER_SERVICE(Imx6Gpio6);
REGISTER_SERVICE(Imx6Gpio7);
REGISTER_SERVICE(Imx6Kpp);
REGISTER_SERVICE(Imx6Sdma);
