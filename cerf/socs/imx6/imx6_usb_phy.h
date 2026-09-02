#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#pragma once

#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../core/service.h"

namespace {

/* i.MX6 USB PHY blocks, one 4 KB aperture each at 0x020C9000 and 0x020CA000.
   QEMU models these as TYPE_IMX_USBPHY children separate from the ANATOP block
   (fsl-imx6.c maps FSL_IMX6_USBPHY1_ADDR + n * 0x1000), and the i.MX6
   reference manual gives the familiar register/SET/CLR/TOG quartet layout:
   PWD, TX, RX, CTRL, STATUS, DEBUG and DEBUG status words. */
template <uint32_t kBase, unsigned kIndex> class Imx6UsbPhy : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    void OnReady() override {
        regs_[kRegPwd >> 4] = 0x00000000u; /* powered by default */
        regs_[kRegTx >> 4] = 0x10060607u;  /* reset-like TX tune values */
        regs_[kRegRx >> 4] = 0x00000000u;
        regs_[kRegCtrl >> 4] = 0x00000000u;   /* SFTRST/CLKGATE deasserted */
        regs_[kRegStatus >> 4] = 0x00000000u; /* no disconnect/line event */
        regs_[kRegDebug >> 4] = 0x7F180000u;  /* squelch/debug reset-ish */
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

    uint32_t MmioBase() const override { return kBase; }


private:
    uint32_t MmioSize() const override { return 0x1000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usbphy read32 unaligned", addr, 0);
        if (!IsModelledRegister(off)) HaltUnsupportedAccess("imx6-usbphy read32 unmodelled register", addr, 0);

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
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usbphy write32 unaligned", addr, value);
        if (!IsModelledRegister(off)) HaltUnsupportedAccess("imx6-usbphy write32 unmodelled register", addr, value);

        const uint32_t reg = off & ~0xFu;
        uint32_t& slot = regs_[reg >> 4];
        switch (off & 0xCu) {
        case 0x0: slot = value; break;
        case 0x4: slot |= value; break;  /* SET */
        case 0x8: slot &= ~value; break; /* CLR */
        case 0xC: slot ^= value; break;  /* TOG */
        }
        if (reg == kRegCtrl) slot &= ~kCtrlSftrst;
    }

    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }

    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    static constexpr uint32_t kRegPwd = 0x00u;
    static constexpr uint32_t kRegTx = 0x10u;
    static constexpr uint32_t kRegRx = 0x20u;
    static constexpr uint32_t kRegCtrl = 0x30u;
    static constexpr uint32_t kRegStatus = 0x40u;
    static constexpr uint32_t kRegDebug = 0x50u;
    static constexpr uint32_t kRegDebug0Status = 0x60u;
    static constexpr uint32_t kRegDebug1 = 0x70u;
    static constexpr uint32_t kCtrlSftrst = 1u << 31;
    static constexpr uint32_t kCtrlClkgate = 1u << 30;

    static bool IsModelledRegister(uint32_t off) {
        if ((off & 3u) != 0) return false;
        const uint32_t reg = off & ~0xFu;
        return reg == kRegPwd || reg == kRegTx || reg == kRegRx || reg == kRegCtrl || reg == kRegStatus ||
               reg == kRegDebug || reg == kRegDebug0Status || reg == kRegDebug1;
    }

    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    uint32_t regs_[0x80u / 0x10u]{};
};

} /* namespace */
