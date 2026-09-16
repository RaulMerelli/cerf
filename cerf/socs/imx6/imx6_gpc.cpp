#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_gic.h"
#include "imx6_mmio_lane.h"

#include <array>

namespace {
class Imx6Gpc final : public Peripheral {
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
    uint32_t MmioBase() const override { return 0x020DC000u; }
    uint32_t MmioSize() const override { return 0x4000u; }
    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x00u) return cntr_;
        if (off >= 0x08u && off <= 0x14u && (off & 3u) == 0u)
            return imr_[(off - 0x08u) >> 2u];
        if (off >= 0x18u && off <= 0x24u && (off & 3u) == 0u) {
            const uint32_t bank = (off - 0x18u) >> 2u;
            return emu_.Get<Imx6Gic>().ReadMmio(0x1204u + bank * 4u);
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { Imx6MergeMmioWrite(*this, addr, value, 1u); }
    void WriteHalf(uint32_t addr, uint16_t value) override { Imx6MergeMmioWrite(*this, addr, value, 2u); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x00u) {
            /* IMX6SDLRM Rev.4 §28.5.1. */
            if ((value & 0x3u) != 0u)
                HaltUnsupportedAccess("GPU/VPU power request", addr, value);
            cntr_ = 0x00100000u | (value & 0x00200000u);
            return;
        }
        if (off >= 0x08u && off <= 0x14u && (off & 3u) == 0u) {
            imr_[(off - 0x08u) >> 2u] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }
    void SaveState(StateWriter& w) override {
        w.Write(cntr_);
        w.WriteBytes(imr_.data(), sizeof(imr_));
    }
    void RestoreState(StateReader& r) override {
        r.Read(cntr_);
        r.ReadBytes(imr_.data(), sizeof(imr_));
    }

private:
    void ResetRegisters() {
        cntr_ = 0x00100000u;
        imr_.fill(0u);
    }

    uint32_t cntr_ = 0x00100000u;
    std::array<uint32_t, 4> imr_{};
};
REGISTER_SERVICE(Imx6Gpc);
} // namespace
