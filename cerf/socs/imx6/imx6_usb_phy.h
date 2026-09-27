#pragma once

#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../core/service.h"
#include "imx6_mmio_lane.h"
#include "imx6_id.h"

namespace cerf_imx6_usb_phy_detail {

/* Linux imx6qdl.dtsi and QEMU fsl-imx6.c map USBPHY1/2 at 0x020C9000
   and 0x020CA000 as separate 4 KiB blocks. */
template <uint32_t kBase, unsigned kIndex> class Imx6UsbPhy : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }

    void OnReady() override {
        /* IMX6DQRM Rev.2 §66.3 USBPHY memory map: PWD 001E_1C00h, TX 1006_0607h, RX 0000_0000h,
           CTRL C020_0000h, STATUS 0000_0000h, DEBUG 7F18_0000h, DEBUG0_STATUS 0000_0000h read-only
           and DEBUG1 0000_1000h. */
        regs_[kRegPwd >> 4] = 0x001E1C00u;
        regs_[kRegTx >> 4] = 0x10060607u;
        regs_[kRegRx >> 4] = 0x00000000u;
        regs_[kRegCtrl >> 4] = 0xC0200000u;
        regs_[kRegStatus >> 4] = 0x00000000u;
        regs_[kRegDebug >> 4] = 0x7F180000u;
        regs_[kRegDebug0Status >> 4] = 0x00000000u;
        regs_[kRegDebug1 >> 4] = 0x00001000u;
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

    uint32_t MmioBase() const override { return kBase; }


private:
    uint32_t MmioSize() const override { return 0x1000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usbphy read32 unaligned", addr, 0);
        if (!IsModelledRegister(off)) HaltUnsupportedAccess("imx6-usbphy read32 unmodelled register", addr, 0);

        const uint32_t reg = off & ~0xFu;
        uint32_t v = regs_[reg >> 4];
        if (reg == kRegCtrl) {
            /* hmi_ktp400_mobile_v17 usbd.dll @0x10006AE4. */
            v = (v & ~kCtrlSftrst) | kCtrlClkgate;
        }
        return v;
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
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usbphy write32 unaligned", addr, value);
        if (!IsModelledRegister(off)) HaltUnsupportedAccess("imx6-usbphy write32 unmodelled register", addr, value);

        const uint32_t reg = off & ~0xFu;
        /* IMX6DQRM Rev.2 §66.3 lists DEBUG0_STATUS as access R. */
        if (reg == kRegDebug0Status)
            HaltUnsupportedAccess("imx6-usbphy write to the read-only DEBUG0_STATUS", addr, value);
        uint32_t& slot = regs_[reg >> 4];
        switch (off & 0xCu) {
        case 0x0: slot = value; break;
        case 0x4: slot |= value; break;
        case 0x8: slot &= ~value; break;
        case 0xC: slot ^= value; break;
        }
        if (reg == kRegCtrl) slot &= ~kCtrlSftrst;
    }

    void SaveState(StateWriter& w) override { w.WriteBytes("regs", regs_, sizeof(regs_)); }

    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_, sizeof(regs_)); }

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

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (!IsModelledRegister(off))
            HaltUnsupportedAccess("imx6-usbphy write lane unmodelled register",
                                  lane.address, lane.value);
        const uint32_t reg = off & ~0xFu;
        if (reg == kRegDebug0Status)
            HaltUnsupportedAccess("imx6-usbphy write to the read-only DEBUG0_STATUS", lane.address, lane.value);
        uint32_t& slot = regs_[reg >> 4];
        switch (off & 0xCu) {
        case 0x0: slot = lane.Merge(slot); break;
        case 0x4: slot |= lane.value; break;
        case 0x8: slot &= ~lane.value; break;
        case 0xC: slot ^= lane.value; break;
        }
        if (reg == kRegCtrl) slot &= ~kCtrlSftrst;
    }

    uint32_t regs_[0x80u / 0x10u]{};
};

}

using cerf_imx6_usb_phy_detail::Imx6UsbPhy;
