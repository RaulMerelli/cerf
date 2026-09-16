#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_mmio_lane.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

class Imx6Weim : public Peripheral {
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

    uint32_t MmioBase() const override { return 0x021B8000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (IsRegister(off)) {
            return regs_[off >> 2];
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { Imx6MergeMmioWrite(*this, addr, value, 1u); }
    void WriteHalf(uint32_t addr, uint16_t value) override { Imx6MergeMmioWrite(*this, addr, value, 2u); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (IsWritableRegister(off)) {
            regs_[off >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }

    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    static bool IsRegister(uint32_t off) {
        return ((off <= 0x5Cu) || (off >= 0x90u && off <= 0xA0u)) &&
               (off & 3u) == 0u;
    }

    static bool IsWritableRegister(uint32_t off) {
        return IsRegister(off) && off != 0x98u;
    }

    void ResetRegisters() {
        std::memset(regs_, 0, sizeof(regs_));
        /* IMX6SDLRM Rev.4 §22.8 and §22.9.1-§22.9.11. */
        for (uint32_t base = 0u; base <= 0x48u; base += 0x18u) {
            regs_[(base + 0x00u) >> 2] = 0x00610088u;
            regs_[(base + 0x04u) >> 2] = 0x00001010u;
            regs_[(base + 0x08u) >> 2] = 0x1C002000u;
            regs_[(base + 0x10u) >> 2] = 0x1C000000u;
        }
        regs_[0x90u >> 2] = 0x00000020u;
        regs_[0x94u >> 2] = 0x01400000u;
        regs_[0x9Cu >> 2] = 0x00000010u;
    }

    uint32_t regs_[0xA4u / 4u]{};
};
}

REGISTER_SERVICE(Imx6Weim);
