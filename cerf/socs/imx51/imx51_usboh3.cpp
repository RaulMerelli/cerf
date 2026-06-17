#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <array>
#include <cstdint>

namespace {

/* i.MX51 USBOH3 (MCIMX51RM Ch 60, base 0x73F80000) — R/W store: OAL PHY config
   (0x800+) + the four ChipIdea/EHCI USB cores (<0x800, 0x200 spacing), each with
   an attached SMSC USB3317 ULPI PHY reached through that core's ULPI Viewport. */
constexpr uint32_t kBase     = 0x73F80000u;
constexpr uint32_t kSize     = 0x00004000u;   /* AIPS 16 KB slot */
constexpr uint32_t kNonCore  = 0x00000800u;   /* non-core control block start */
constexpr uint32_t kCoreSpan = 0x00000200u;   /* per-core EHCI block spacing */
constexpr uint32_t kCores    = kNonCore / kCoreSpan;

constexpr uint32_t kOffUsbcmd = 0x00000140u;  /* USBCMD within each core (EHCI) */
constexpr uint32_t kUsbcmdReset = 1u << 1;    /* USBCMD.RST */
constexpr uint32_t kOffCaplen = 0x00000100u;  /* CAPLENGTH(b0)+HCIVERSION(b16) */
/* CAPLENGTH 0x40 (operational regs at cap+0x40 = core+0x140) | HCIVERSION 0x0100
   (EHCI 1.00, MCIMX51RM Ch 60 VUSB_HS_HCIVERSION); the EHCI driver reads
   CAPLENGTH (byte) to locate the operational registers. */
constexpr uint32_t kCapReset = 0x01000040u;

/* USBCMD enable bits / USBSTS status bits, MCIMX51RM Ch 60 Table 60-40 (USBCMD,
   Fig 60-34) + Table 60-41 (USBSTS, Fig 60-15). The host-controller status bits
   in USBSTS must converge to the enable bits the driver writes in USBCMD; CERF
   acts instantly so it reflects them on the USBCMD write. */
constexpr uint32_t kOffUsbsts = 0x00000144u;  /* USBSTS within each core (=USBCMD+4) */
constexpr uint32_t kCmdRs  = 1u << 0;   /* USBCMD.RS  Run/Stop */
constexpr uint32_t kCmdPse = 1u << 4;   /* USBCMD.PSE Periodic Schedule Enable */
constexpr uint32_t kCmdAse = 1u << 5;   /* USBCMD.ASE Asynchronous Schedule Enable */
constexpr uint32_t kStsHch = 1u << 12;  /* USBSTS.HCH HCHalted (RO; set when RS=0) */
constexpr uint32_t kStsPss = 1u << 14;  /* USBSTS.PS  Periodic Schedule Status (RO==PSE) */
constexpr uint32_t kStsAss = 1u << 15;  /* USBSTS.AS  Asynchronous Schedule Status (RO==ASE) */
/* USBSTS read-only host-controller bits (HCHalted, Reclamation b13, PSS, ASS):
   a guest USBSTS write (write-1-clear of the interrupt bits, e.g. 0xF03F) must
   leave these untouched — they are owned by ReflectScheduleStatus. */
constexpr uint32_t kUsbstsRoMask = kStsHch | (1u << 13) | kStsPss | kStsAss;

constexpr uint32_t kOffUlpiview = 0x00000170u;  /* ULPI Viewport within each core */
constexpr uint32_t kUlpiWu  = 1u << 31;  /* ULPIWU  (wakeup)  */
constexpr uint32_t kUlpiRun = 1u << 30;  /* ULPIRUN (xfer)    */
constexpr uint32_t kUlpiRw  = 1u << 29;  /* ULPIRW  (1=write) */

constexpr uint8_t kPhyRegCount = 0x40u;
/* SMSC USB3317 ULPI Vendor ID 0x0424 / Product ID 0x0006 (USB331x family, USB3317
   datasheet); the driver's PHY-presence check reads regs 0..3 and requires both
   non-zero (sub_C0C5769C/sub_C0C576D4 via BSPUsbCheckPhyAccess sub_C0C58010). */
constexpr uint8_t kUsb3317Id[4] = {0x24u, 0x04u, 0x06u, 0x00u};

class Imx51Usboh3 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::iMX51;
    }
    void OnReady() override {
        for (uint32_t core = 0; core < kNonCore; core += kCoreSpan) {
            regs_[(core + kOffCaplen) >> 2] = kCapReset;
            /* Out of reset the host controller is halted: USBCMD.RS reset = 0
               (Table 60-40) ⇒ USBSTS.HCHalted = 1 (Table 60-41). */
            regs_[(core + kOffUsbsts) >> 2] = kStsHch;
        }
        for (auto& phy : phy_)
            for (uint8_t i = 0; i < 4; ++i) phy[i] = kUsb3317Id[i];
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        return static_cast<uint8_t>(regs_[off >> 2] >> ((off & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        return static_cast<uint16_t>(regs_[off >> 2] >> ((off & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override { return regs_[(addr - kBase) >> 2]; }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        if (off < kNonCore) {
            const uint32_t coff = off % kCoreSpan;
            if (coff == kOffUsbcmd) {
                /* USBCMD.RST self-clears at reset completion (else the host
                   driver spins in its reset poll). After the write, reflect the
                   schedule/run enables into USBSTS. */
                value &= ~kUsbcmdReset;
                regs_[off >> 2] = value;
                ReflectScheduleStatus(off, value);
                return;
            }
            if (coff == kOffUsbsts) {
                /* USBSTS interrupt bits are write-1-clear; the RO status bits
                   (kUsbstsRoMask) are host-controller-owned and untouched by the
                   guest write. MCIMX51RM Ch 60 Table 60-41. */
                const uint32_t old = regs_[off >> 2];
                regs_[off >> 2] =
                    (old & kUsbstsRoMask) | (old & ~kUsbstsRoMask & ~value);
                return;
            }
            if (coff == kOffUlpiview) {
                /* The ULPI Viewport access completes instantly; without it the
                   driver spins in its PHY-access poll and reboots. */
                regs_[off >> 2] = UlpiTransfer(off / kCoreSpan, value);
                return;
            }
        }
        regs_[off >> 2] = value;
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes(regs_.data(), sizeof(regs_));
        w.WriteBytes(phy_.data(), sizeof(phy_));
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes(regs_.data(), sizeof(regs_));
        r.ReadBytes(phy_.data(), sizeof(phy_));
    }

private:
    /* Reflect USBCMD.ASE(b5)/PSE(b4) into USBSTS.ASS(b15)/PSS(b14) and set HCHalted(b12)
       when RS(b0)=0 — CERF has no schedule engine, so the status bits match instantly.
       Else hcd_hsh1!sub_C0C64B6C spins (while((USBCMD ^ (USBSTS>>10)) & 0x20) Sleep(1) =
       wait ASS==ASE) and devmgr's ActivateDevice never returns -> no gwes. RM Ch60 T60-40/41. */
    void ReflectScheduleStatus(uint32_t usbcmd_off, uint32_t usbcmd) {
        const uint32_t i = (usbcmd_off >> 2) + 1;  /* USBSTS = USBCMD + 4 */
        uint32_t s = regs_[i];
        s = (usbcmd & kCmdAse) ? (s | kStsAss) : (s & ~kStsAss);
        s = (usbcmd & kCmdPse) ? (s | kStsPss) : (s & ~kStsPss);
        s = (usbcmd & kCmdRs)  ? (s & ~kStsHch) : (s | kStsHch);
        regs_[i] = s;
    }

    /* Run one ULPI Viewport access against the core's USB3317 PHY register file and
       return the value to latch: RUN/WU clear on completion; a read returns the PHY
       register in ULPIDATRD (bits 15:8). (USB3317_ReadReg sub_C0C57574,
       WriteReg sub_C0C57608.) */
    uint32_t UlpiTransfer(uint32_t core, uint32_t value) {
        if (value & kUlpiWu)      return value & ~kUlpiWu;
        if (!(value & kUlpiRun))  return value;
        auto& phy = phy_[core % kCores];
        const uint8_t addr = static_cast<uint8_t>(value >> 16) & (kPhyRegCount - 1);
        if (value & kUlpiRw) {
            phy[addr] = static_cast<uint8_t>(value);
            return value & ~kUlpiRun;
        }
        return (value & ~kUlpiRun & ~0xFF00u) | (static_cast<uint32_t>(phy[addr]) << 8);
    }

    std::array<uint32_t, kSize / 4> regs_{};
    std::array<std::array<uint8_t, kPhyRegCount>, kCores> phy_{};
};

}  /* namespace */

REGISTER_SERVICE(Imx51Usboh3);
