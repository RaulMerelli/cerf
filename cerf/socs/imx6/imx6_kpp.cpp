#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/imx6/imx6_gic.h"
#include "../../state/state_stream.h"
#include "imx6_id.h"

namespace {

class Imx6Kpp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }
    void OnReady() override {
        kpsr_ = 0;
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
        case kKpsr: value = kpsr_ & (kKpkd | kKpkr | kKdie | kKrie); break;
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
        /* IMX6DQRM Rev.2 §38.6.1 and §38.3.2 Table 38-3: KCO[15:8] selects open-drain against
           totem-pole drive on a column strobe, which is a pad property; CERF models the block. */
        case kKpcr:
            kpcr_ = value;
            UpdateKeyState();
            return;
        case kKpsr:
            if ((value & ~(kKpkd | kKpkr | kKdsc | kKrss | kKdie | kKrie)) != 0u)
                HaltUnsupportedAccess("imx6-kpp KPSR reserved bit write", addr, value);
            kpsr_ = static_cast<uint16_t>(kpsr_ & ~(value & (kKpkd | kKpkr)));
            kpsr_ = static_cast<uint16_t>((kpsr_ & ~(kKdie | kKrie)) | (value & (kKdie | kKrie)));
            /* IMX6DQRM Rev.2 §38.6.2: KDSC clears the key depress synchronizer chain and KRSS sets
               the key release synchronizer chain; both self-clear and read back as 0. */
            if (value & kKdsc) depress_sync_ = false;
            if (value & kKrss) release_sync_ = true;
            UpdateKeyState();
            return;
        case kKddr:
            kddr_ = value;
            UpdateKeyState();
            return;
        case kKpdr:
            kpdr_latch_ = value;
            UpdateKeyState();
            return;
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
        w.Write("kpcr", kpcr_);
        w.Write("kpsr", kpsr_);
        w.Write("kddr", kddr_);
        w.Write("kpdr_latch", kpdr_latch_);
        w.Write<uint8_t>("sync", static_cast<uint8_t>((depress_sync_ ? 1u : 0u) | (release_sync_ ? 2u : 0u)));
    }
    void RestoreState(StateReader& r) override {
        r.Read("kpcr", kpcr_);
        r.Read("kpsr", kpsr_);
        r.Read("kddr", kddr_);
        r.Read("kpdr_latch", kpdr_latch_);
        uint8_t sync = 0;
        r.Read("sync", sync);
        depress_sync_ = (sync & 1u) != 0u;
        release_sync_ = (sync & 2u) != 0u;
    }

    void PostRestore() override { UpdateIrq(); }

private:
    static constexpr uint32_t kKpcr = 0x00u;
    static constexpr uint32_t kKpsr = 0x02u;
    static constexpr uint32_t kKddr = 0x04u;
    static constexpr uint32_t kKpdr = 0x06u;

    static constexpr uint16_t kKpkd = 0x0001u;
    static constexpr uint16_t kKpkr = 0x0002u;
    static constexpr uint16_t kKdie = 0x0100u;
    static constexpr uint16_t kKrie = 0x0200u;
    static constexpr uint16_t kKdsc = 0x0004u;
    static constexpr uint16_t kKrss = 0x0008u;

    /* IMX6DQRM Rev.2 §38.3.1: only the eight row inputs carry the internal pull-up. §38.3.2
       Table 38-3 leaves a column strobe with KDDR clear an input with no pull-up, so its level is
       a pad and board property that CERF does not model. */
    static constexpr uint16_t kColumnInputAbsentStub = 0xFF00u;

    uint16_t ReadKpdr() const {
        const uint16_t row_inputs = static_cast<uint16_t>(~kddr_ & 0x00FFu);
        const uint16_t column_inputs = static_cast<uint16_t>(~kddr_ & 0xFF00u);
        return static_cast<uint16_t>((kpdr_latch_ & kddr_) | row_inputs |
                                     (column_inputs & kColumnInputAbsentStub));
    }

    /* IMX6DQRM Rev.2 §38.6.1: KRE selects the rows that take part in detection. §38.6.2: KPKD
       follows one or more enabled rows low and KPKR all enabled rows high, both through the
       synchronizer chains of §38.4.5, whose S-R latch is set on a rising edge. */
    void UpdateKeyState() {
        const uint16_t enabled_rows = static_cast<uint16_t>(kpcr_ & 0x00FFu);
        bool depress = false;
        bool release = false;
        if (enabled_rows != 0u) {
            const uint16_t rows = static_cast<uint16_t>(ReadKpdr() & 0x00FFu);
            depress = (rows & enabled_rows) != enabled_rows;
            release = !depress;
        }
        if (depress && !depress_sync_) kpsr_ = static_cast<uint16_t>(kpsr_ | kKpkd);
        if (release && !release_sync_) kpsr_ = static_cast<uint16_t>(kpsr_ | kKpkr);
        depress_sync_ = depress;
        release_sync_ = release;
        UpdateIrq();
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
    uint16_t kpsr_ = 0;
    uint16_t kddr_ = 0;
    uint16_t kpdr_latch_ = 0xFFFFu;
    bool depress_sync_ = false;
    bool release_sync_ = false;
};

REGISTER_SERVICE(Imx6Kpp);

} // namespace
