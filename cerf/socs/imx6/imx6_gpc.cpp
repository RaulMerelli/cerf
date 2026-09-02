#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

namespace {
class Imx6Gpc final : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().RegisterResettable(this); }
    uint32_t MmioBase() const override { return 0x020DC000u; }
    uint32_t MmioSize() const override { return 0x4000u; }
    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0) return off == 0u ? regs_[0] & ~3u : regs_[off >> 2];
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { MergeWrite(addr, value, 1); }
    void WriteHalf(uint32_t addr, uint16_t value) override { MergeWrite(addr, value, 2); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0) {
            regs_[off >> 2] = off == 0u ? value & ~3u : value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }
    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }
    uint32_t regs_[0x4000u / 4u]{};
};
REGISTER_SERVICE(Imx6Gpc);
} // namespace
