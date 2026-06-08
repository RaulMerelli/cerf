#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* SA-1110 Dev Man §11.13: PPDR/PPSR/PPAR/PSDR/PPFR at +0x00..0x10. +0x28 is
   reserved but HPIrDA.dll sub_EE4B88 RMWs it during IrDA config; real HW reads
   it 0 / ignores writes without aborting. Other undocumented offsets halt. */

class Sa11xxPpc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && (bd->GetSoc() == SocFamily::SA1110 || bd->GetSoc() == SocFamily::SA1100);
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x90060000u; }
    uint32_t MmioSize() const override { return 0x00010000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

private:
    static constexpr uint32_t kReservedIrdaPoke = 0x28u;  /* HPIrDA sub_EE4B88. */
    static constexpr uint32_t kMccr1Offset      = 0x30u;  /* SA-1110 Dev Man: MCCR1 R/W. */

    uint32_t regs_[5] = {};  /* PPDR, PPSR, PPAR, PSDR, PPFR */

    /* MCCR1 (MCP control reg 1) shares this block; the kernel disables the MCP
       by writing 0 here during clock setup (nk.exe sub_80039150). Stored R/W. */
    uint32_t mccr1_ = 0;

    static bool OffsetToIndex(uint32_t off, uint32_t* index_out) {
        if (off > 0x10 || (off & 0x3u) != 0) return false;
        *index_out = off / 4u;
        return true;
    }
};

uint8_t Sa11xxPpc::ReadByte(uint32_t addr) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    uint32_t index;
    if (OffsetToIndex(base, &index))
        return static_cast<uint8_t>((regs_[index] >> shift) & 0xFFu);
    if (base == kMccr1Offset)
        return static_cast<uint8_t>((mccr1_ >> shift) & 0xFFu);
    if (base == kReservedIrdaPoke) {
        LOG(Periph, "[Sa11xxPpc] reserved read +0x%02X -> 0\n", off);
        return 0;
    }
    HaltUnsupportedAccess("ReadByte", addr, 0);
}

uint32_t Sa11xxPpc::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    uint32_t index;
    if (OffsetToIndex(off, &index)) return regs_[index];
    if (off == kMccr1Offset) return mccr1_;
    if (off == kReservedIrdaPoke) {
        LOG(Periph, "[Sa11xxPpc] reserved read +0x%02X -> 0\n", off);
        return 0;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Sa11xxPpc::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    uint32_t index;
    if (OffsetToIndex(base, &index)) {
        const uint32_t cur     = regs_[index];
        const uint32_t cleared = cur & ~(0xFFu << shift);
        regs_[index] = cleared | (static_cast<uint32_t>(value) << shift);
        return;
    }
    if (base == kMccr1Offset) {
        const uint32_t cleared = mccr1_ & ~(0xFFu << shift);
        mccr1_ = cleared | (static_cast<uint32_t>(value) << shift);
        return;
    }
    if (base == kReservedIrdaPoke) {
        LOG(Periph, "[Sa11xxPpc] reserved write +0x%02X (ignored)\n", base);
        return;
    }
    HaltUnsupportedAccess("WriteByte", addr, value);
}

void Sa11xxPpc::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    uint32_t index;
    if (OffsetToIndex(off, &index)) { regs_[index] = value; return; }
    if (off == kMccr1Offset) { mccr1_ = value; return; }
    if (off == kReservedIrdaPoke) {
        LOG(Periph, "[Sa11xxPpc] reserved write +0x%02X = 0x%08X (ignored)\n", off, value);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

}  /* namespace */

REGISTER_SERVICE(Sa11xxPpc);
