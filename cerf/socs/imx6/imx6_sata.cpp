#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* IMX6DQRM Rev.2 Table 2-3: SATA at 0x0220_0000..0x0220_3FFF. */
constexpr uint32_t kSataBase = 0x02200000u;
constexpr uint32_t kSataSize = 0x00004000u;

/* IMX6DQRM Rev.2 §53 memory map: SATA_P0SCTL at +0x12C and SATA_P0PHYCR at +0x178, both reset 0. */
constexpr uint32_t kOffP0Sctl = 0x12Cu;
constexpr uint32_t kOffP0Phycr = 0x178u;

/* IMX6DQRM Rev.2 SATA_P0SCTL: DET is bits 3:0; 0x1 requests COMRESET. */
constexpr uint32_t kSctlDetMask = 0xFu;
constexpr uint32_t kSctlDetComreset = 0x1u;
/* IMX6DQRM Rev.2 SATA_P0PHYCR: bits 19:16 are CR_READ/CR_WRITE/CR_CAP_DATA/CR_CAP_ADDR, bit 20 TEST_PDDQ. */
constexpr uint32_t kPhycrCrStrobes = 0x000F0000u;

class Imx6Sata final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().RegisterResettable(this); }

    uint32_t MmioBase() const override { return kSataBase; }
    uint32_t MmioSize() const override { return kSataSize; }

    /* hmi_ktp400_mobile_v13 nk.exe sub_8030DF1C @ VA 0x8030DF1C reads and writes back
       P0SCTL and P0PHYCR with 32-bit accesses when DIGPROG reports i.MX 6Dual/6Quad. */
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kSataBase;
        if (off == kOffP0Sctl) return p0_sctl_;
        if (off == kOffP0Phycr) return p0_phycr_;
        HaltUnsupportedAccess("imx6-sata read32 unmodelled register", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kSataBase;
        if (off == kOffP0Sctl) {
            if ((value & kSctlDetMask) == kSctlDetComreset)
                HaltUnsupportedAccess("imx6-sata P0SCTL COMRESET", addr, value);
            p0_sctl_ = value;
            return;
        }
        if (off == kOffP0Phycr) {
            if ((value & kPhycrCrStrobes) != 0u)
                HaltUnsupportedAccess("imx6-sata P0PHYCR control-register strobe", addr, value);
            p0_phycr_ = value;
            return;
        }
        HaltUnsupportedAccess("imx6-sata write32 unmodelled register", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write(p0_sctl_);
        w.Write(p0_phycr_);
    }
    void RestoreState(StateReader& r) override {
        r.Read(p0_sctl_);
        r.Read(p0_phycr_);
    }

private:
    uint32_t p0_sctl_ = 0u;
    uint32_t p0_phycr_ = 0u;
};

}

REGISTER_SERVICE(Imx6Sata);
