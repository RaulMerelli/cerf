#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../core/service.h"

namespace {

class Imx6Anatop : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        /* IMX6SDLRM §18.7.1: on Solo/DualLite, PFD_480=0xF0 and PFD_528=0x100. */
        regs_[0x000 / 0x10] = 0x80002042u; /* USB1_PLL480 reset (enable + lock) */
        regs_[0x010 / 0x10] = 0x80003000u; /* USB2_PLL480 reset */
        regs_[0x020 / 0x10] = 0x80003000u; /* PLL_SYS (PLL2_528) reset, lock=1 */
        regs_[0x030 / 0x10] = 0x80002001u; /* PLL_USB1_BYPASS / PLL_USB1_CTRL */
        regs_[0x070 / 0x10] = 0x00011006u; /* PLL_AUDIO, DIV_SELECT=6, PD=1 */
        regs_[0x080 / 0x10] = 0x05F5E100u; /* PLL_AUDIO_NUM */
        regs_[0x090 / 0x10] = 0x2964619Cu; /* PLL_AUDIO_DENOM */
        regs_[0x0A0 / 0x10] = 0x0001100Cu; /* PLL_VIDEO, DIV_SELECT=12, PD=1 */
        regs_[0x0B0 / 0x10] = 0x05F5E100u; /* PLL_VIDEO_NUM */
        regs_[0x0C0 / 0x10] = 0x2964619Cu; /* PLL_VIDEO_DENOM */
        regs_[0x0E0 / 0x10] = 0x80002001u; /* PLL_ENET, ENET_25M_REF_EN, DIV=1 */
        /* PFD packed 8-bit fractions. The BSP enumerates these words and
           deliberately UDFs if an ungated fraction byte is zero, so every
           byte must be a valid PFD_FRAC (range 12..35 per IMX6SDLRM §18.7).
           Reset values per IMX6SDLRM Tab 18-21/22. */
        regs_[0x0F0 / 0x10] = 0x1311100Cu; /* PFD_480 — PFD0..3 FRAC=12/16/17/19
                                              -> PLL3 720/540/508/454 MHz */
        /* PFD_528 — PFD0..3 FRAC=27/16/24/16 -> PLL2 352/594/396/594 MHz, the
           standard i.MX6 fractions (Linux clk-imx6q, IMX6SDLRM Tab 18-22). */
        regs_[0x100 / 0x10] = 0x1018101Bu;
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }
    uint32_t MmioBase() const override { return 0x020C8000u; }
    uint32_t MmioSize() const override { return 0x1000u; }
    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        /* DIGPROG bits [23:16]=product, [7:0]=revision; 0x61 = Solo/DualLite die. Wrong product ID fails BSP die-family
         * check. */
        if (off == 0x260u) return 0x00610001u;
        /* Alternate DIGPROG location used by the SL/SX probe path.  This is
           a Solo/DualLite die, so leave that signature absent and let the
           BSP fall back to the 0x260 register above. */
        if (off == 0x280u) return 0u;
        if (off < 0x1000u && (off & 3u) == 0) {
            uint32_t v = regs_[(off & ~0xFu) / 0x10u];
            return v;
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { MergeWrite(addr, value, 1); }
    void WriteHalf(uint32_t addr, uint16_t value) override { MergeWrite(addr, value, 2); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x260u) return; /* DIGPROG read-only. */
        if (off == 0x280u) return; /* alternate DIGPROG read-only. */
        if (off < 0x1000u && (off & 3u) == 0) {
            uint32_t& reg = regs_[(off & ~0xFu) / 0x10u];
            switch (off & 0xCu) {
            case 0x0: reg = value; break;
            case 0x4: reg |= value; break;
            case 0x8: reg &= ~value; break;
            case 0xC: reg ^= value; break;
            }
            const uint32_t base = off & ~0xFu;
            if (base == 0x00u || base == 0x10u || base == 0x20u || base == 0x30u || base == 0xE0u) reg |= 0x80000000u;
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
    uint32_t regs_[0x4000u / 0x10u]{};
};

} /* namespace */

REGISTER_SERVICE(Imx6Anatop);
