#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../core/service.h"
#include "imx6_mmio_lane.h"


namespace {

class Imx6IomuxcGpr : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        /* IMX6DQRM Rev.2 §36.4: GPR1 resets to 4840_0005h, GPR2 to 0000_0000h. */
        gpr1_ = 0x48400005u;
        gpr2_ = 0x00000000u;
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

    uint32_t MmioBase() const override { return 0x020E0000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x004u) return gpr1_;
        if (off == 0x008u) return gpr2_;
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { Imx6MergeMmioWrite(*this, addr, value, 1u); }
    void WriteHalf(uint32_t addr, uint16_t value) override { Imx6MergeMmioWrite(*this, addr, value, 2u); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        /* IMX6DQRM Rev.2 §36.4: the pad mux, pad control and daisy-chain registers select which
           on-chip block drives each external pin. CERF models the blocks, not the pins, so the
           selection has no pad to reach; the guest reads back only GPR1 and GPR2. */
        if (IsRegister(off)) {
            if (off == 0x004u) gpr1_ = value;
            else if (off == 0x008u) gpr2_ = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write(gpr1_);
        w.Write(gpr2_);
    }

    void RestoreState(StateReader& r) override {
        r.Read(gpr1_);
        r.Read(gpr2_);
    }

private:
    /* IMX6DQRM Rev.2 §36.4, IOMUXC memory map: registers at 0x000-0x034, 0x04C-0x7E8 and
       0x7F0-0x94C. */
    static bool IsRegister(uint32_t off) {
        if ((off & 3u) != 0u) return false;
        return off <= 0x034u || (off >= 0x04Cu && off <= 0x7E8u) || (off >= 0x7F0u && off <= 0x94Cu);
    }

    uint32_t gpr1_ = 0;
    uint32_t gpr2_ = 0;
};

}

REGISTER_SERVICE(Imx6IomuxcGpr);
