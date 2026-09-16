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
#include "../../cpu/emulated_memory.h"

namespace {

/* IMX6SDLRM Rev.4 Chapter 65 defines the USB core and USBNC register windows. */
class Imx6Usb : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        for (uint32_t core = 0; core < kNonCore; core += kCoreSpan) {
            regs_[(core + kOffCaplen) >> 2] = kCapReset;
            regs_[(core + kOffHcsparams) >> 2] = 0x00010011u;
            regs_[(core + kOffHccparams) >> 2] = 0x00000006u;
            regs_[(core + kOffDciversion) >> 2] = 0x00000001u;
            regs_[(core + kOffDccparams) >> 2] = 0x00000188u;
            regs_[(core + kOffUsbsts) >> 2] = kStsHch;
            regs_[(core + kOffPortsc1) >> 2] = 0x00001000u;
        }
        phy_[0] = 0x24u;
        phy_[1] = 0x04u;
        phy_[2] = 0x06u;
        phy_[3] = 0x00u;
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

    uint32_t MmioBase() const override { return 0x02184000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usb read32 unaligned", addr, 0);
        if (!IsModelledOffset(off)) HaltUnsupportedAccess("imx6-usb read32 unmodelled register", addr, 0);
        return regs_[off >> 2];
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
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usb write32 unaligned", addr, value);
        if (!IsModelledOffset(off)) HaltUnsupportedAccess("imx6-usb write32 unmodelled register", addr, value);
        const uint32_t coff = (off < kNonCore) ? (off % kCoreSpan) : off;
        const uint32_t core = off - coff;
        if (off < kNonCore && coff == kOffUsbcmd) {
            value &= ~kUsbcmdReset;
            regs_[off >> 2] = value;
            ReflectScheduleStatus(core, value);
            return;
        }
        if (off < kNonCore && coff == kOffUsbsts) {
            const uint32_t old = regs_[off >> 2];
            regs_[off >> 2] = (old & kUsbstsRoMask) | (old & ~kUsbstsRoMask & ~value);
            return;
        }
        if (core == 0u && coff == kOffUlpiview) {
            regs_[off >> 2] = UlpiTransfer(value);
            return;
        }
        regs_[off >> 2] = value;
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes(regs_, sizeof(regs_));
        w.WriteBytes(phy_, sizeof(phy_));
    }

    void RestoreState(StateReader& r) override {
        r.ReadBytes(regs_, sizeof(regs_));
        r.ReadBytes(phy_, sizeof(phy_));
    }

private:
    /* IMX6SDLRM Rev.4 Chapter 65 maps four USB cores at 0x200 strides from
       0x02184000 and USBNC_USB_OTG_CTRL at 0x02184800. */
    static constexpr uint32_t kCoreSpan = 0x200u;
    static constexpr uint32_t kNonCore = 0x800u;

    static constexpr uint32_t kOffCaplen = 0x100u;
    static constexpr uint32_t kOffHcsparams = 0x104u;
    static constexpr uint32_t kOffHccparams = 0x108u;
    static constexpr uint32_t kOffDciversion = 0x120u;
    static constexpr uint32_t kOffDccparams = 0x124u;
    static constexpr uint32_t kOffUsbcmd = 0x140u;
    static constexpr uint32_t kOffUsbsts = 0x144u;
    static constexpr uint32_t kOffUsbintr = 0x148u;
    static constexpr uint32_t kOffFrindex = 0x14Cu;
    static constexpr uint32_t kOffCtrlDsSeg = 0x150u;
    static constexpr uint32_t kOffDeviceAddr = 0x154u;
    static constexpr uint32_t kOffAsyncList = 0x158u;
    static constexpr uint32_t kOffTtCtrl = 0x15Cu;
    static constexpr uint32_t kOffBurstSize = 0x160u;
    static constexpr uint32_t kOffTxFill = 0x164u;
    static constexpr uint32_t kOffUlpiview = 0x170u;
    static constexpr uint32_t kOffConfigFlg = 0x180u;
    static constexpr uint32_t kOffPortsc1 = 0x184u;
    static constexpr uint32_t kOffOtgsc = 0x1A4u;
    static constexpr uint32_t kOffUsbmode = 0x1A8u;
    static constexpr uint32_t kCapReset = 0x01000040u;
    static constexpr uint32_t kUsbcmdReset = 1u << 1;
    static constexpr uint32_t kCmdRs = 1u << 0;
    static constexpr uint32_t kCmdPse = 1u << 4;
    static constexpr uint32_t kCmdAse = 1u << 5;
    static constexpr uint32_t kStsHch = 1u << 12;
    static constexpr uint32_t kStsPss = 1u << 14;
    static constexpr uint32_t kStsAss = 1u << 15;
    static constexpr uint32_t kUsbstsRoMask = kStsHch | (1u << 13) | kStsPss | kStsAss;
    static constexpr uint32_t kUlpiWu = 1u << 31;
    static constexpr uint32_t kUlpiRun = 1u << 30;
    static constexpr uint32_t kUlpiRw = 1u << 29;

    static bool IsEndpointOffset(uint32_t off) { return off >= 0x1ACu && off <= 0x1DCu && (off & 3u) == 0; }

    static bool IsUsbncOffset(uint32_t off) {
        switch (off) {
        case 0x800u:
        case 0x804u:
        case 0x808u:
        case 0x80Cu:
        case 0x810u:
        case 0x814u:
        case 0x818u:
        case 0x81Cu:
        case 0x820u:
        case 0x824u:
        case 0x828u:
        case 0x82Cu:
        case 0x830u:
        case 0x834u:
        case 0x838u:
        case 0x83Cu: return true;
        default: return false;
        }
    }

    static bool IsModelledOffset(uint32_t off) {
        if (off >= kNonCore) return IsUsbncOffset(off);
        switch (off % kCoreSpan) {
        case kOffCaplen:
        case kOffHcsparams:
        case kOffHccparams:
        case kOffDciversion:
        case kOffDccparams:
        case kOffUsbcmd:
        case kOffUsbsts:
        case kOffUsbintr:
        case kOffFrindex:
        case kOffCtrlDsSeg:
        case kOffDeviceAddr:
        case kOffAsyncList:
        case kOffTtCtrl:
        case kOffBurstSize:
        case kOffTxFill:
        case kOffUlpiview:
        case kOffConfigFlg:
        case kOffPortsc1:
        case kOffOtgsc:
        case kOffUsbmode: return true;
        default: return IsEndpointOffset(off % kCoreSpan);
        }
    }

    void ReflectScheduleStatus(uint32_t core, uint32_t usbcmd) {
        uint32_t s = regs_[(core + kOffUsbsts) >> 2];
        s = (usbcmd & kCmdAse) ? (s | kStsAss) : (s & ~kStsAss);
        s = (usbcmd & kCmdPse) ? (s | kStsPss) : (s & ~kStsPss);
        s = (usbcmd & kCmdRs) ? (s & ~kStsHch) : (s | kStsHch);
        regs_[(core + kOffUsbsts) >> 2] = s;
    }

    uint32_t UlpiTransfer(uint32_t value) {
        if (value & kUlpiWu) return value & ~kUlpiWu;
        if (!(value & kUlpiRun)) return value;
        const uint8_t reg = static_cast<uint8_t>(value >> 16) & 0x3Fu;
        if (value & kUlpiRw) {
            phy_[reg] = static_cast<uint8_t>(value);
            return value & ~kUlpiRun;
        }
        return (value & ~kUlpiRun & ~0xFF00u) | (static_cast<uint32_t>(phy_[reg]) << 8);
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        const uint32_t coff = off < kNonCore ? off % kCoreSpan : off;
        if (off < kNonCore && coff == kOffUsbsts) {
            const uint32_t old = regs_[off >> 2];
            regs_[off >> 2] = (old & kUsbstsRoMask) |
                               (old & ~kUsbstsRoMask & ~lane.value);
            return;
        }
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

    uint32_t regs_[0x4000u / 4u]{};
    uint8_t phy_[0x40u]{};
};
}

REGISTER_SERVICE(Imx6Usb);
