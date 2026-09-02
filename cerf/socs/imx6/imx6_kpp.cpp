#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/imx6/imx6_gic.h"
#include "../../state/state_stream.h"

namespace {

class Imx6Kpp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        kpsr_ = kKpsrReset;
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
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
        case kKpcr: value = kpcr_; break;
        case kKpsr: value = kpsr_ & (kKpkd | kKpkr | kKdie | kKrie | kKdsr); break;
        case kKddr: value = kddr_; break;
        case kKpdr: value = ReadKpdr(); break;
        default: HaltUnsupportedAccess("imx6-kpp read16 unmodelled register", addr, 0);
        }
        return value;
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 1u) != 0u) HaltUnsupportedAccess("imx6-kpp read32 unaligned", addr, 0);
        return uint32_t(ReadHalf(addr)) | (uint32_t(ReadHalf(addr + 2u)) << 16);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t aligned = addr & ~1u;
        const uint16_t old = ReadHalf(aligned);
        const uint16_t merged = (addr & 1u) ? static_cast<uint16_t>((old & 0x00FFu) | (uint16_t(value) << 8))
                                            : static_cast<uint16_t>((old & 0xFF00u) | value);
        WriteHalf(aligned, merged);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case kKpcr: kpcr_ = value; return;
        case kKpsr:
            /* KPKD/KPKR are W1C status bits; KDIE/KRIE are plain enables.
               KDSC/KRSS are synchronization strobes and self-clear. */
            kpsr_ = static_cast<uint16_t>(kpsr_ & ~(value & (kKpkd | kKpkr)));
            kpsr_ = static_cast<uint16_t>((kpsr_ & ~(kKdie | kKrie)) | (value & (kKdie | kKrie)) | kKpsrReset);
            UpdateIrq();
            return;
        case kKddr: kddr_ = value; return;
        case kKpdr: kpdr_latch_ = value; return;
        default: HaltUnsupportedAccess("imx6-kpp write16 unmodelled register", addr, value);
        }
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 1u) != 0u) HaltUnsupportedAccess("imx6-kpp write32 unaligned", addr, value);
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
        const bool desired = ((kpsr_ & kKpkd) && (kpsr_ & kKdie)) || ((kpsr_ & kKpkr) && (kpsr_ & kKrie));
        auto& gic = emu_.Get<Imx6Gic>();
        if (desired)
            gic.AssertSpi(82);
        else
            gic.DeAssertSpi(82);
    }

    uint16_t kpcr_ = 0;
    uint16_t kpsr_ = kKpsrReset;
    uint16_t kddr_ = 0;
    uint16_t kpdr_latch_ = 0xFFFFu;
};

REGISTER_SERVICE(Imx6Kpp);

} // namespace
