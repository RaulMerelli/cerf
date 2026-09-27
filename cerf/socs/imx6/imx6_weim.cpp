#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_mmio_lane.h"

#include <cstdint>
#include "imx6_id.h"

namespace {

class Imx6Weim : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }
    void OnReady() override {
        /* IMX6DQRM Rev.2 §22.9: CS0GCR1 reset value. */
        cs0gcr1_ = 0x00610088u;
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
        if (off == 0x00u) return cs0gcr1_;
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { Imx6MergeMmioWrite(*this, addr, value, 1u); }
    void WriteHalf(uint32_t addr, uint16_t value) override { Imx6MergeMmioWrite(*this, addr, value, 2u); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        /* IMX6DQRM Rev.2 §22.9: these registers set the timing of an external EIM device. No board
           modelled here wires one, so the programmed timing has no bus to act on; the guest reads
           back only CS0GCR1. */
        if (IsRegister(off)) {
            if (off == 0x00u) cs0gcr1_ = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override { w.Write("cs0gcr1", cs0gcr1_); }

    void RestoreState(StateReader& r) override { r.Read("cs0gcr1", cs0gcr1_); }

private:
    /* IMX6DQRM Rev.2 §22.9: CS0..CS3 GCR1..WCR2 at 0x00-0x5C, WCR/WIAR/EAR at 0x90-0x98, all R/W. */
    static bool IsRegister(uint32_t off) {
        return ((off <= 0x5Cu) || (off >= 0x90u && off <= 0x98u)) &&
               (off & 3u) == 0u;
    }

    uint32_t cs0gcr1_ = 0;
};
}

REGISTER_SERVICE(Imx6Weim);
