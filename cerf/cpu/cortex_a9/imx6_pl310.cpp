#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

namespace {
class Imx6Pl310 final : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        regs_[0x104u >> 2] = 0x02020000u;
        regs_[0x108u >> 2] = regs_[0x10Cu >> 2] = 0x111u;
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }
    uint32_t MmioBase() const override { return 0x00A02000u; }
    uint32_t MmioSize() const override { return 0x1000u; }
    uint8_t ReadByte(uint32_t address) override {
        return static_cast<uint8_t>(ReadWord(address & ~3u) >> ((address & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t address) override {
        return static_cast<uint16_t>(ReadWord(address & ~3u) >> ((address & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t address) override {
        const uint32_t offset = address - MmioBase();
        if (offset == 0u) return 0x410000C8u;
        if (offset == 4u) return 0x1C100100u;
        if (IsRegister(offset)) return regs_[offset >> 2];
        HaltUnsupportedAccess("imx6-pl310 read32 unmodelled register", address, 0);
    }
    void WriteByte(uint32_t address, uint8_t value) override { MergeWrite(address, value, 1u); }
    void WriteHalf(uint32_t address, uint16_t value) override { MergeWrite(address, value, 2u); }
    void WriteWord(uint32_t address, uint32_t value) override {
        const uint32_t offset = address - MmioBase();
        if (!IsRegister(offset)) HaltUnsupportedAccess("imx6-pl310 write32 unmodelled register", address, value);
        if (offset == 0x100u) value &= 1u;
        if (IsMaintenanceOperation(offset)) {
            /* ARM DDI 0246F, sections 3.1.1 and 3.3.10: the line and
               Cache-Sync operations are atomic, while the three by-Way
               registers clear each selected Way bit as that background
               operation completes.  CERF has no data-bearing L2 cache, so
               every maintenance operation completes synchronously and its
               observable register value is zero by the next guest read. */
            regs_[offset >> 2] = 0u;
        } else if (offset == 0x21Cu)
            regs_[offset >> 2] &= ~value;
        else
            regs_[offset >> 2] = value;
    }
    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    static bool IsMaintenanceOperation(uint32_t offset) {
        switch (offset) {
        case 0x730u: /* Cache Sync */
        case 0x770u: /* Invalidate Line by PA */
        case 0x77Cu: /* Invalidate by Way */
        case 0x7B0u: /* Clean Line by PA */
        case 0x7B8u: /* Clean Line by Set/Way */
        case 0x7BCu: /* Clean by Way */
        case 0x7F0u: /* Clean and Invalidate Line by PA */
        case 0x7F8u: /* Clean and Invalidate Line by Set/Way */
        case 0x7FCu: /* Clean and Invalidate by Way */ return true;
        default: return false;
        }
    }

    static bool IsRegister(uint32_t offset) {
        switch (offset) {
        case 0x100u:
        case 0x104u:
        case 0x108u:
        case 0x10Cu:
        case 0x200u:
        case 0x204u:
        case 0x208u:
        case 0x20Cu:
        case 0x210u:
        case 0x214u:
        case 0x218u:
        case 0x21Cu:
        case 0x220u:
        case 0x730u:
        case 0x740u:
        case 0x770u:
        case 0x77Cu:
        case 0x7B0u:
        case 0x7B8u:
        case 0x7BCu:
        case 0x7F0u:
        case 0x7F8u:
        case 0x7FCu:
        case 0xF40u:
        case 0xF60u:
        case 0xF80u: return true;
        default: return false;
        }
    }
    void MergeWrite(uint32_t address, uint32_t value, uint32_t width) {
        const uint32_t aligned = address & ~3u;
        const uint32_t shift = (address & 3u) * 8u;
        const uint32_t mask = (width == 1u ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }
    uint32_t regs_[0x1000u / 4u]{};
};
REGISTER_SERVICE(Imx6Pl310);
} // namespace
