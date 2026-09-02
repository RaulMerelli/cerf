#pragma once

#include "../../peripherals/peripheral_base.h"

#include "siemens_mp377_sm501_internal.h"
#include "siemens_mp377_ertec400.h"
#include "siemens_mp377_debug_leds.h"
#include "siemens_mp377_mram.h"
#include "siemens_mp377_aspc2.h"
#include "siemens_mp377_power_reset.h"

#include "../../core/cerf_emulator.h"
#include "../../jit/arm/arm_jit.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../board_context.h"

#include <atomic>
#include <cstdint>

/* Siemens MP 377 Board Peripheral Interface — board-decoded MMIO bands
   declared by the P377 OAL OEMAddressTable (nk.exe @ 0x80409F00):

     VA=0x90000000 PA=0xF0000000  16 MB   FPGA/CPLD board ID + HwInfo
     VA=0x91000000 PA=0xF2000000  32 MB   external bus 1 (touch / PBI)
     VA=0x94000000 PA=0xC0000000 128 MB   ATU primary outbound mem
     VA=0x9C000000 PA=0xD0000000  64 MB   ATU secondary outbound mem
 */
namespace mp377_board_io_detail {

using siemens_mp377::kMp377Aspc2Base;
using siemens_mp377::kMp377Aspc2RamPa;
using siemens_mp377::kMp377Aspc2RamSize;
using siemens_mp377::kMp377Aspc2Size;
using siemens_mp377::kMp377MramBase;
using siemens_mp377::kMp377MramSize;
using siemens_mp377::kMp377PowerResetBase;
using siemens_mp377::kMp377PowerResetEnd;
using siemens_mp377::Mp377SmiBridgeWindowId;
using siemens_mp377::SmiBridgeBase;
using siemens_mp377::SmiBridgeEnd;

class Mp377BoardIoWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint8_t ReadByte(uint32_t addr) override { HaltUnsupportedAccess("MP377 unsupported board byte read", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override {
        HaltUnsupportedAccess("MP377 unsupported board halfword read", addr, 0);
    }
    uint32_t ReadWord(uint32_t addr) override { HaltUnsupportedAccess("MP377 unsupported board word read", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("MP377 unsupported board byte write", addr, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("MP377 unsupported board halfword write", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        HaltUnsupportedAccess("MP377 unsupported board word write", addr, value);
    }

};

constexpr uint32_t kEbus1Base = 0xF2000000u;
constexpr uint32_t kEbus1End = 0xF4000000u;

constexpr uint32_t kAtuPrimaryBase = 0xC0000000u;
constexpr uint32_t kAtuPrimaryEnd = 0xC8000000u;

/* WinCE/P377 OAL static-map view of the ATU primary outbound window.
   The SM501 model registers its driver-visible CPU BARs directly at
   0xC0000000 (BAR1 regs) and 0xC2000000 (BAR0 FB), matching the values that
   smibase/VGXaudio pass to MmMapIoSpace/CreateStaticMapping.  Keep the guard
   splits around those direct devices; do not install an additional forwarding
   bridge over the same CPU PA ranges. */
constexpr uint32_t kAtuPrimarySm501RegsBase = 0xC0000000u;
constexpr uint32_t kAtuPrimarySm501RegsEnd = kAtuPrimarySm501RegsBase + siemens_mp377::kSm501RegsBytes;
constexpr uint32_t kAtuPrimarySm501FbBase = 0xC2000000u;
constexpr uint32_t kAtuPrimarySm501FbEnd = kAtuPrimarySm501FbBase + siemens_mp377::kSm501FbBytes;

/* The guest's own static-mapping PTE translates the early console VA
   0xB8000000 to PA 0xC4100000 (siemens_mp377_v1040, nk.exe FF-fill at
   PC=0x804426BC, R0=0xB8000000 R2=0x000EA5F0: stores land at
   0xC4100000..0xC41EA5F0).  Forward that band to modelled VRAM; the SMI
   bridge register window at 0xC4100028..0xC4100038 is carved out by the
   guard split around it, so only genuine framebuffer stores reach VRAM. */
constexpr uint32_t kAtuPrimaryConsoleBase = 0xC4100000u;

inline bool InAtuVramAlias(uint32_t addr, uint32_t bytes) {
    return bytes != 0u && addr >= kAtuPrimaryConsoleBase &&
           (addr - kAtuPrimaryConsoleBase) + bytes <= siemens_mp377::kSm501FbBytes;
}

inline uint32_t AtuConsoleVramOffset(uint32_t addr) {
    return addr - kAtuPrimaryConsoleBase;
}

inline uint32_t AtuPrimarySm501RegsBusAddr(uint32_t addr) {
    return siemens_mp377::kSm501RegsBarBus + (addr - kAtuPrimarySm501RegsBase);
}

inline uint32_t AtuPrimarySm501FbBusAddr(uint32_t addr) {
    return siemens_mp377::kSm501FbBarBus + (addr - kAtuPrimarySm501FbBase);
}

class SiemensMp377AtuOutboundPrimaryGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;

    void WriteByte(uint32_t addr, uint8_t value) override {
        /* P377 OAL maps the early RGB565 software framebuffer through this
           ATU aperture; forward every in-BAR access to modelled VRAM. */
        if (InAtuVramAlias(addr, 1u)) {
            auto* video = emu_.TryGet<siemens_mp377::SiemensMp377Sm501Video>();
            if (video && video->WriteVramByte(AtuConsoleVramOffset(addr), value)) return;
        }
        Mp377BoardIoWindow::WriteByte(addr, value);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (InAtuVramAlias(addr, 2u)) {
            auto* video = emu_.TryGet<siemens_mp377::SiemensMp377Sm501Video>();
            if (video && video->WriteVramHalf(AtuConsoleVramOffset(addr), value)) return;
        }
        Mp377BoardIoWindow::WriteHalf(addr, value);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (InAtuVramAlias(addr, 4u)) {
            auto* video = emu_.TryGet<siemens_mp377::SiemensMp377Sm501Video>();
            if (video && video->WriteVramWord(AtuConsoleVramOffset(addr), value)) return;
        }
        Mp377BoardIoWindow::WriteWord(addr, value);
    }

private:
    bool CoversAccess(uint32_t addr, uint32_t bytes) const {
        const uint32_t end = MmioBase() + MmioSize();
        return bytes != 0u && addr >= MmioBase() && addr + bytes - 1u < end;
    }
};

/* The ATU secondary outbound window is split around MRAM, the discovered
   power/reset block at 0xD0180000..0xD0183000, and the OneNAND chip at
   0xD0200000..0xD0280000.  PeripheralDispatcher rejects overlaps. */

/* PA 0xFF000000+16MB band, flanking the IOP13xx PMMR at 0xFFD80000+1MB. */

} // namespace mp377_board_io_detail
