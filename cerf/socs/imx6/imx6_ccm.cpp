#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../core/service.h"
#include "imx6_mmio_lane.h"

#include <algorithm>
#include <iterator>
#include "imx6_id.h"

namespace {

class Imx6Ccm : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }
    void OnReady() override {
        ResetRegisters();
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }
    uint32_t MmioBase() const override { return 0x020C4000u; }
    uint32_t MmioSize() const override { return 0x4000u; }
    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (IsRegister(off)) return regs_[off >> 2];
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, 1u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, 2u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        WriteLane({addr & ~3u, value, 0xFFFFFFFFu});
    }

    void SaveState(StateWriter& w) override { w.WriteBytes("regs", regs_, sizeof(regs_)); }

    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_, sizeof(regs_)); }

private:
    static bool IsRegister(uint32_t off) {
        if ((off & 3u) != 0u) return false;
        if (off == 0x00u || off == 0x50u) return true;
        if (off >= 0x10u && off <= 0x3Cu) return true;
        return off >= 0x68u && off <= 0x80u;
    }

    static bool IsWritableRegister(uint32_t off) {
        switch (off) {
        case 0x18u:
        case 0x50u:
        case 0x6Cu:
        case 0x70u:
        case 0x74u:
        case 0x7Cu:
        case 0x80u:
            return true;
        default:
            return false;
        }
    }

    void ResetRegisters() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        /* IMX6DQRM Rev.2 Table 60-4 resets functional modules on POR, COLD, and WARM;
           §18.6 gives one CCM reset-value column and notes ROM may change its values. */
        regs_[0x00u >> 2] = 0x040116FFu;
        regs_[0x14u >> 2] = 0x00018D00u;
        regs_[0x18u >> 2] = 0x00020324u;
        regs_[0x1Cu >> 2] = 0x00F00000u;
        regs_[0x20u >> 2] = 0x02B92F06u;
        regs_[0x24u >> 2] = 0x00490B00u;
        regs_[0x28u >> 2] = 0x0EC102C1u;
        regs_[0x2Cu >> 2] = 0x000736C1u;
        regs_[0x30u >> 2] = 0x33F71F92u;
        regs_[0x34u >> 2] = 0x0002A150u;
        regs_[0x38u >> 2] = 0x0002A150u;
        regs_[0x3Cu >> 2] = 0x00010841u;
        /* QEMU i.MX6 CCM model resets CCM_CTOR to 0; hmi_ktp400_mobile_v17
           nk.exe 0x803187C2 clears bits 7:4 then sets bit 13. */
        regs_[0x50u >> 2] = 0x00000000u;
        regs_[0x68u >> 2] = 0xFFFFFFFFu;
        regs_[0x6Cu >> 2] = 0xFFFFFFFFu;
        regs_[0x70u >> 2] = 0xFC3FFFFFu;
        regs_[0x74u >> 2] = 0xFFFFFFFFu;
        regs_[0x78u >> 2] = 0xFFFFFFFFu;
        regs_[0x7Cu >> 2] = 0xFFFFFFFFu;
        regs_[0x80u >> 2] = 0xFFFFFFFFu;
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (IsWritableRegister(off)) {
            regs_[off >> 2] = lane.Merge(regs_[off >> 2]);
            return;
        }
        HaltUnsupportedAccess("write lane", lane.address, lane.value);
    }
    uint32_t regs_[0x8Cu / 4u]{};
};

}

REGISTER_SERVICE(Imx6Ccm);
