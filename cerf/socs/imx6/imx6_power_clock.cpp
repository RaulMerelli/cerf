#include "../../core/cerf_emulator.h"
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
        /* IMX6SDLRM Â§18.7.1: on Solo/DualLite, PFD_480=0xF0 and PFD_528=0x100. */
        regs_[0x000 / 0x10] = 0x80002042u;  /* USB1_PLL480 reset (enable + lock) */
        regs_[0x010 / 0x10] = 0x80003000u;  /* USB2_PLL480 reset */
        regs_[0x020 / 0x10] = 0x80003000u;  /* PLL_SYS (PLL2_528) reset, lock=1 */
        regs_[0x030 / 0x10] = 0x80002001u;  /* PLL_USB1_BYPASS / PLL_USB1_CTRL */
        regs_[0x070 / 0x10] = 0x00011006u;  /* PLL_AUDIO, DIV_SELECT=6, PD=1 */
        regs_[0x080 / 0x10] = 0x05F5E100u;  /* PLL_AUDIO_NUM */
        regs_[0x090 / 0x10] = 0x2964619Cu;  /* PLL_AUDIO_DENOM */
        regs_[0x0A0 / 0x10] = 0x0001100Cu;  /* PLL_VIDEO, DIV_SELECT=12, PD=1 */
        regs_[0x0B0 / 0x10] = 0x05F5E100u;  /* PLL_VIDEO_NUM */
        regs_[0x0C0 / 0x10] = 0x2964619Cu;  /* PLL_VIDEO_DENOM */
        regs_[0x0E0 / 0x10] = 0x80002001u;  /* PLL_ENET, ENET_25M_REF_EN, DIV=1 */
        /* PFD packed 8-bit fractions. The BSP enumerates these words and
           deliberately UDFs if an ungated fraction byte is zero, so every
           byte must be a valid PFD_FRAC (range 12..35 per IMX6SDLRM Â§18.7).
           Reset values per IMX6SDLRM Tab 18-21/22. */
        regs_[0x0F0 / 0x10] = 0x1311100Cu;  /* PFD_480 â€” PFD0..3 FRAC=12/16/17/19
                                               -> PLL3 720/540/508/454 MHz */
        /* PFD_528 â€” PFD0..3 FRAC=27/16/24/16 -> PLL2 352/594/396/594 MHz, the
           standard i.MX6 fractions (Linux clk-imx6q, IMX6SDLRM Tab 18-22).
           Was 0x18131815 (FRAC 21/24/19/24 -> non-standard 452/396/500 MHz),
           which fed wrong derived peripheral clocks. */
        regs_[0x100 / 0x10] = 0x1018101Bu;
        emu_.Get<PeripheralDispatcher>().Register(this);
    }
    uint32_t MmioBase() const override { return 0x020C8000u; }
    uint32_t MmioSize() const override { return 0x1000u; }
    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(
            ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(
            ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        /* DIGPROG bits [23:16]=product, [7:0]=revision; 0x61 = Solo/DualLite die. Wrong product ID fails BSP die-family check. */
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
    void WriteByte(uint32_t addr, uint8_t value) override {
        MergeWrite(addr, value, 1);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        MergeWrite(addr, value, 2);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x260u) return;   /* DIGPROG read-only. */
        if (off == 0x280u) return;   /* alternate DIGPROG read-only. */
        if (off < 0x1000u && (off & 3u) == 0) {
            uint32_t& reg = regs_[(off & ~0xFu) / 0x10u];
            switch (off & 0xCu) {
            case 0x0: reg = value; break;
            case 0x4: reg |= value; break;
            case 0x8: reg &= ~value; break;
            case 0xC: reg ^= value; break;
            }
            const uint32_t base = off & ~0xFu;
            if (base == 0x00u || base == 0x10u || base == 0x20u ||
                base == 0x30u || base == 0xE0u)
                reg |= 0x80000000u;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }
private:
    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask =
            (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned,
            (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }
    uint32_t regs_[0x4000u / 0x10u]{};
};

/* i.MX6 USB PHY blocks, one 4 KB aperture each at 0x020C9000 and 0x020CA000.
   QEMU models these as TYPE_IMX_USBPHY children separate from the ANATOP block
   (fsl-imx6.c maps FSL_IMX6_USBPHY1_ADDR + n * 0x1000), and the i.MX6
   reference manual gives the familiar register/SET/CLR/TOG quartet layout:
   PWD, TX, RX, CTRL, STATUS, DEBUG and DEBUG status words. */
template<uint32_t kBase, unsigned kIndex>
class Imx6UsbPhy : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    void OnReady() override {
        regs_[kRegPwd >> 4] = 0x00000000u;       /* powered by default */
        regs_[kRegTx  >> 4] = 0x10060607u;       /* reset-like TX tune values */
        regs_[kRegRx  >> 4] = 0x00000000u;
        regs_[kRegCtrl >> 4] = 0x00000000u;      /* SFTRST/CLKGATE deasserted */
        regs_[kRegStatus >> 4] = 0x00000000u;    /* no disconnect/line event */
        regs_[kRegDebug >> 4] = 0x7F180000u;     /* squelch/debug reset-ish */
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x1000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0)
            HaltUnsupportedAccess("imx6-usbphy read32 unaligned", addr, 0);
        if (!IsModelledRegister(off))
            HaltUnsupportedAccess("imx6-usbphy read32 unmodelled register", addr, 0);

        const uint32_t reg = off & ~0xFu;
        uint32_t v = regs_[reg >> 4];
        if (reg == kRegCtrl) {
            /* The Freescale WinCE host driver asserts SFTRST through CTRL_SET
               and then polls CTRL bit 30 before continuing.  The earlier
               monolithic ANATOP model exposed this as the PHY clock/reset
               acknowledge bit; keep that observable hardware completion while
               retaining the dedicated USBPHY register block shape used by
               QEMU/Linux/datasheet. */
            v = (v & ~kCtrlSftrst) | kCtrlClkgate;
        }
        return v;
    }
    void WriteByte(uint32_t addr, uint8_t value) override { MergeWrite(addr, value, 1); }
    void WriteHalf(uint32_t addr, uint16_t value) override { MergeWrite(addr, value, 2); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0)
            HaltUnsupportedAccess("imx6-usbphy write32 unaligned", addr, value);
        if (!IsModelledRegister(off))
            HaltUnsupportedAccess("imx6-usbphy write32 unmodelled register", addr, value);

        const uint32_t reg = off & ~0xFu;
        uint32_t& slot = regs_[reg >> 4];
        switch (off & 0xCu) {
        case 0x0: slot = value; break;
        case 0x4: slot |= value; break;      /* SET */
        case 0x8: slot &= ~value; break;     /* CLR */
        case 0xC: slot ^= value; break;      /* TOG */
        }
        if (reg == kRegCtrl)
            slot &= ~kCtrlSftrst;

    }

private:
    static constexpr uint32_t kRegPwd    = 0x00u;
    static constexpr uint32_t kRegTx     = 0x10u;
    static constexpr uint32_t kRegRx     = 0x20u;
    static constexpr uint32_t kRegCtrl   = 0x30u;
    static constexpr uint32_t kRegStatus = 0x40u;
    static constexpr uint32_t kRegDebug  = 0x50u;
    static constexpr uint32_t kRegDebug0Status = 0x60u;
    static constexpr uint32_t kRegDebug1 = 0x70u;
    static constexpr uint32_t kCtrlSftrst = 1u << 31;
    static constexpr uint32_t kCtrlClkgate = 1u << 30;

    static bool IsModelledRegister(uint32_t off) {
        if ((off & 3u) != 0) return false;
        const uint32_t reg = off & ~0xFu;
        return reg == kRegPwd || reg == kRegTx || reg == kRegRx ||
               reg == kRegCtrl || reg == kRegStatus || reg == kRegDebug ||
               reg == kRegDebug0Status || reg == kRegDebug1;
    }

    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    uint32_t regs_[0x80u / 0x10u]{};
};

using Imx6UsbPhy0 = Imx6UsbPhy<0x020C9000u, 0>;
using Imx6UsbPhy1 = Imx6UsbPhy<0x020CA000u, 1>;

class Imx6Ccm : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        /* CCM hardware reset values, IMX6SDLRM Rev.1 Â§18.7 Table 18-5..18-23.
           Critical: divider POST fields must be non-zero where the CE OAL
           clock-tree dumper asserts (cbnz/UDF #0xF9 at nk.exe+0xB9A2). */
        regs_[0x00u >> 2] = 0x040116FFu;  /* CCR  */
        regs_[0x04u >> 2] = 0x00000000u;  /* CCDR */
        regs_[0x08u >> 2] = 0x00000010u;  /* CSR  */
        regs_[0x0Cu >> 2] = 0x00000100u;  /* CCSR */
        regs_[0x10u >> 2] = 0x00000000u;  /* CACRR */
        regs_[0x14u >> 2] = 0x00018D40u;  /* CBCDR â€” periph2 from PLL2, ARM/AHB/IPG divs */
        regs_[0x18u >> 2] = 0x00022324u;  /* CBCMR â€” POR (IMX6SDLRM/QEMU);
                                             bit13 gpu3d_core_clk_sel was dropped */
        regs_[0x1Cu >> 2] = 0x00F00000u;  /* CSCMR1 */
        regs_[0x20u >> 2] = 0x02B92F06u;  /* CSCMR2 */
        regs_[0x24u >> 2] = 0x00490B00u;  /* CSCDR1 â€” uSDHC PODF, UART PODF=0 (Ã·1) */
        regs_[0x28u >> 2] = 0x0EC102C1u;  /* CS1CDR */
        regs_[0x2Cu >> 2] = 0x000736C1u;  /* CS2CDR */
        regs_[0x30u >> 2] = 0x33F71F92u;  /* CDCDR */
        regs_[0x34u >> 2] = 0x0002A150u;  /* CHSCCDR */
        regs_[0x38u >> 2] = 0x0002A150u;  /* CSCDR2 */
        regs_[0x3Cu >> 2] = 0x00014841u;  /* CSCDR3 */
        regs_[0x40u >> 2] = 0x00000000u;  /* CSCDR4 */
        regs_[0x44u >> 2] = 0x00000000u;  /* CWDR */
        regs_[0x48u >> 2] = 0x00000000u;  /* CDHIPR â€” idle */
        regs_[0x4Cu >> 2] = 0x00000000u;
        regs_[0x50u >> 2] = 0x00000000u;
        regs_[0x54u >> 2] = 0x00000079u;  /* CLPCR */
        regs_[0x58u >> 2] = 0x00000000u;  /* CISR */
        regs_[0x5Cu >> 2] = 0xFFFFFFFFu;  /* CIMR */
        regs_[0x60u >> 2] = 0x000A0001u;  /* CCOSR */
        regs_[0x64u >> 2] = 0x0000FE62u;  /* CGPR */
        /* CCGR0..CCGR6 (0x68..0x80): all IP clocks running, default for CE init. */
        for (uint32_t off = 0x68u; off <= 0x80u; off += 4u)
            regs_[off >> 2] = 0xFFFFFFFFu;
        regs_[0x84u >> 2] = 0x00000000u;  /* CMEOR */
        emu_.Get<PeripheralDispatcher>().Register(this);
    }
    uint32_t MmioBase() const override { return 0x020C4000u; }
    uint32_t MmioSize() const override { return 0x4000u; }
    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(
            ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(
            ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x48u) return 0u;  /* CDHIPR: no divider handshake busy */
        if (off <= 0x8Cu && (off & 3u) == 0) return regs_[off >> 2];
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        MergeWrite(addr, value, 1);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        MergeWrite(addr, value, 2);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x48u) return;
        if (off <= 0x8Cu && (off & 3u) == 0) {
            regs_[off >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }
private:
    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask =
            (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned,
            (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }
    uint32_t regs_[0x90u / 4u]{};
};

/* IOMUXC general-purpose, mux-select and pad-control registers. CERF models
   no physical pads here, but the BSP writes these words during device
   initialization and may read them back; retain the whole 16 KB aperture. */
class Imx6IomuxcGpr : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x020E0000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(
            ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(
            ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0) {
            return regs_[off >> 2];
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        MergeWrite(addr, value, 1);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        MergeWrite(addr, value, 2);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0) {
            regs_[off >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

private:
    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask =
            (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    uint32_t regs_[0x4000u / 4u]{};
};

}  /* namespace */

REGISTER_SERVICE(Imx6Anatop);
REGISTER_SERVICE(Imx6UsbPhy0);
REGISTER_SERVICE(Imx6UsbPhy1);
REGISTER_SERVICE(Imx6Ccm);
REGISTER_SERVICE(Imx6IomuxcGpr);
