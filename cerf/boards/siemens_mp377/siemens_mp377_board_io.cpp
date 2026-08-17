#include "../../peripherals/peripheral_base.h"

#include "siemens_mp377_sm501_internal.h"
#include "siemens_mp377_ertec400.h"
#include "siemens_mp377_debug_leds.h"
#include "siemens_mp377_mram.h"
#include "siemens_mp377_aspc2.h"
#include "siemens_mp377_power_reset.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../board_context.h"

#include <cstdint>

/* Siemens MP 377 Board Peripheral Interface — board-decoded MMIO bands
   declared by the P377 OAL OEMAddressTable (nk.exe @ 0x80409F00):

     VA=0x90000000 PA=0xF0000000  16 MB   FPGA/CPLD board ID + HwInfo
     VA=0x91000000 PA=0xF2000000  32 MB   external bus 1 (touch / PBI)
     VA=0x94000000 PA=0xC0000000 128 MB   ATU primary outbound mem
     VA=0x9C000000 PA=0xD0000000  64 MB   ATU secondary outbound mem

   Only the HWI handoff bytes currently reached by the MP377 boot path are
   modelled here. Other board/ATU bands are registered as loud placeholders so
   the next real access stops at its source instead of being hidden by a
   zero-return silent handler. */
namespace {

using siemens_mp377::kMp377MramBase;
using siemens_mp377::kMp377MramSize;
using siemens_mp377::kMp377Aspc2Base;
using siemens_mp377::kMp377Aspc2Size;
using siemens_mp377::kMp377Aspc2RamPa;
using siemens_mp377::kMp377Aspc2RamSize;
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

    uint8_t ReadByte(uint32_t addr) override {
        HaltUnsupportedAccess("MP377 unsupported board byte read", addr, 0);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        HaltUnsupportedAccess("MP377 unsupported board halfword read", addr, 0);
    }
    uint32_t ReadWord(uint32_t addr) override {
        HaltUnsupportedAccess("MP377 unsupported board word read", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("MP377 unsupported board byte write", addr, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("MP377 unsupported board halfword write", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        HaltUnsupportedAccess("MP377 unsupported board word write", addr, value);
    }

protected:
};

constexpr uint32_t kEbus1Base = 0xF2000000u;
constexpr uint32_t kEbus1End  = 0xF4000000u;

class SiemensMp377Ebus1LowGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return kEbus1Base; }
    uint32_t MmioSize() const override { return siemens_mp377::kDebugLedProgressBase - MmioBase(); }
protected:
};

class SiemensMp377Ebus1MidGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return siemens_mp377::kDebugLedProgressEnd; }
    uint32_t MmioSize() const override { return siemens_mp377::kDebugLedTickBase - MmioBase(); }
protected:
};

class SiemensMp377Ebus1HighGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return siemens_mp377::kDebugLedTickEnd; }
    uint32_t MmioSize() const override { return kEbus1End - MmioBase(); }
protected:
};

constexpr uint32_t kAtuPrimaryBase = 0xC0000000u;
constexpr uint32_t kAtuPrimaryEnd  = 0xC8000000u;

/* WinCE/P377 OAL static-map view of the ATU primary outbound window.
   The SM501 model now registers its driver-visible CPU BARs directly at
   0xC0000000 (BAR1 regs) and 0xC2000000 (BAR0 FB), matching the values that
   smibase/VGXaudio pass to MmMapIoSpace/CreateStaticMapping.  Keep the guard
   splits around those direct devices; do not install an additional forwarding
   bridge over the same CPU PA ranges. */
constexpr uint32_t kAtuPrimarySm501RegsBase = 0xC0000000u;
constexpr uint32_t kAtuPrimarySm501RegsEnd  = kAtuPrimarySm501RegsBase + siemens_mp377::kSm501RegsBytes;
constexpr uint32_t kAtuPrimarySm501FbBase   = 0xC2000000u;
constexpr uint32_t kAtuPrimarySm501FbEnd    = kAtuPrimarySm501FbBase + siemens_mp377::kSm501FbBytes;

/* MP377 nk.exe OEMInit (sub_8044420C) calls OALPAtoVA(0xC4000000, 0),
   passes the returned VA to ConsoleDraw::Init (sub_80445F78), then clears
   width * height * 2 bytes through sub_80445E00.  sub_80446E14 reports the
   MP377 mode as 800x600x16, so the early console occupies the first MiB of
   this ATU-primary aperture. */
constexpr uint32_t kAtuPrimaryConsoleBase = 0xC4000000u;
constexpr uint32_t kAtuPrimaryConsoleEnd  = 0xC4100000u;

static bool IsAtuConsoleWriteByte(uint32_t addr) {
    return addr >= kAtuPrimaryConsoleBase && addr < kAtuPrimaryConsoleEnd;
}

static bool IsAtuConsoleWriteHalf(uint32_t addr) {
    return addr >= kAtuPrimaryConsoleBase && addr + 1u < kAtuPrimaryConsoleEnd;
}

static bool IsAtuConsoleWriteWord(uint32_t addr) {
    return addr >= kAtuPrimaryConsoleBase && addr + 3u < kAtuPrimaryConsoleEnd;
}

static uint32_t AtuConsoleVramOffset(uint32_t addr) {
    return addr - kAtuPrimaryConsoleBase;
}





static uint32_t AtuPrimarySm501RegsBusAddr(uint32_t addr) {
    return siemens_mp377::kSm501RegsBarBus + (addr - kAtuPrimarySm501RegsBase);
}

static uint32_t AtuPrimarySm501FbBusAddr(uint32_t addr) {
    return siemens_mp377::kSm501FbBarBus + (addr - kAtuPrimarySm501FbBase);
}

class SiemensMp377AtuPrimarySm501RegsBridge : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;

    uint32_t MmioBase() const override { return kAtuPrimarySm501RegsBase; }
    uint32_t MmioSize() const override { return siemens_mp377::kSm501RegsBytes; }


    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        const uint8_t value = emu_.Get<siemens_mp377::SiemensMp377Sm501Regs>().ReadByte(bus);
        return value;
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        const uint16_t value = emu_.Get<siemens_mp377::SiemensMp377Sm501Regs>().ReadHalf(bus);
        return value;
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        const uint32_t value = emu_.Get<siemens_mp377::SiemensMp377Sm501Regs>().ReadWord(bus);
        return value;
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        emu_.Get<siemens_mp377::SiemensMp377Sm501Regs>().WriteByte(bus, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        emu_.Get<siemens_mp377::SiemensMp377Sm501Regs>().WriteHalf(bus, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        emu_.Get<siemens_mp377::SiemensMp377Sm501Regs>().WriteWord(bus, value);
    }

protected:
};

class SiemensMp377AtuPrimarySm501FbBridge : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;

    uint32_t MmioBase() const override { return kAtuPrimarySm501FbBase; }
    uint32_t MmioSize() const override { return siemens_mp377::kSm501FbBytes; }


    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t bus = AtuPrimarySm501FbBusAddr(addr);
        const uint8_t value = emu_.Get<siemens_mp377::SiemensMp377Sm501Fb>().ReadByte(bus);
        return value;
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t bus = AtuPrimarySm501FbBusAddr(addr);
        const uint16_t value = emu_.Get<siemens_mp377::SiemensMp377Sm501Fb>().ReadHalf(bus);
        return value;
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t bus = AtuPrimarySm501FbBusAddr(addr);
        const uint32_t value = emu_.Get<siemens_mp377::SiemensMp377Sm501Fb>().ReadWord(bus);
        return value;
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t bus = AtuPrimarySm501FbBusAddr(addr);
        emu_.Get<siemens_mp377::SiemensMp377Sm501Fb>().WriteByte(bus, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t bus = AtuPrimarySm501FbBusAddr(addr);
        emu_.Get<siemens_mp377::SiemensMp377Sm501Fb>().WriteHalf(bus, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t bus = AtuPrimarySm501FbBusAddr(addr);
        emu_.Get<siemens_mp377::SiemensMp377Sm501Fb>().WriteWord(bus, value);
    }

protected:
};


class SiemensMp377AtuOutboundPrimaryGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;


    void WriteByte(uint32_t addr, uint8_t value) override {
        /* P377 OAL maps an early text console / framebuffer aperture at
           VA 0xB8000000, which translates through the ATU primary outbound
           OAT entry to PA 0xC4000000.  For the HWI-selected MP377 panel this
           is the early RGB565 software framebuffer path.  Treat this aperture
           as a software-visible alias of SM501 VRAM instead of dropping writes:
           otherwise CE boots and emits NKDBG text, but the host renderer has
           no guest frame to present. */
        if (CoversAccess(addr, 1u) && IsAtuConsoleWriteByte(addr)) {
            auto* video = emu_.TryGet<siemens_mp377::SiemensMp377Sm501Video>();
            if (video && video->WriteVramByte(AtuConsoleVramOffset(addr), value)) return;
        }
        Mp377BoardIoWindow::WriteByte(addr, value);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (CoversAccess(addr, 2u) && IsAtuConsoleWriteHalf(addr)) {
            auto* video = emu_.TryGet<siemens_mp377::SiemensMp377Sm501Video>();
            if (video && video->WriteVramHalf(AtuConsoleVramOffset(addr), value)) return;
        }
        Mp377BoardIoWindow::WriteHalf(addr, value);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (CoversAccess(addr, 4u) && IsAtuConsoleWriteWord(addr)) {
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

class SiemensMp377AtuOutboundPrimaryAfterRegsPreFbGuard : public SiemensMp377AtuOutboundPrimaryGuard {
public:
    using SiemensMp377AtuOutboundPrimaryGuard::SiemensMp377AtuOutboundPrimaryGuard;
    uint32_t MmioBase() const override { return kAtuPrimarySm501RegsEnd; }
    uint32_t MmioSize() const override { return kAtuPrimarySm501FbBase - MmioBase(); }
};

class SiemensMp377AtuOutboundPrimaryAfterFbPreBridgeGuard : public SiemensMp377AtuOutboundPrimaryGuard {
public:
    using SiemensMp377AtuOutboundPrimaryGuard::SiemensMp377AtuOutboundPrimaryGuard;
    uint32_t MmioBase() const override { return kAtuPrimarySm501FbEnd; }
    uint32_t MmioSize() const override { return SmiBridgeBase(Mp377SmiBridgeWindowId::C410) - MmioBase(); }
};

class SiemensMp377AtuOutboundPrimaryBetweenBridgeGuard : public SiemensMp377AtuOutboundPrimaryGuard {
public:
    using SiemensMp377AtuOutboundPrimaryGuard::SiemensMp377AtuOutboundPrimaryGuard;
    uint32_t MmioBase() const override { return SmiBridgeEnd(Mp377SmiBridgeWindowId::C410); }
    uint32_t MmioSize() const override { return SmiBridgeBase(Mp377SmiBridgeWindowId::C480) - MmioBase(); }
};

class SiemensMp377AtuOutboundPrimaryHighPreErtecGuard : public SiemensMp377AtuOutboundPrimaryGuard {
public:
    using SiemensMp377AtuOutboundPrimaryGuard::SiemensMp377AtuOutboundPrimaryGuard;
    uint32_t MmioBase() const override { return SmiBridgeEnd(Mp377SmiBridgeWindowId::C480); }
    uint32_t MmioSize() const override { return siemens_mp377::kErtecSmallBarsBase - MmioBase(); }
};

class SiemensMp377AtuOutboundPrimaryHighBetweenErtecGuard : public SiemensMp377AtuOutboundPrimaryGuard {
public:
    using SiemensMp377AtuOutboundPrimaryGuard::SiemensMp377AtuOutboundPrimaryGuard;
    uint32_t MmioBase() const override { return siemens_mp377::kErtecSmallWindowEnd; }
    uint32_t MmioSize() const override { return siemens_mp377::kErtecBar3WindowBase - MmioBase(); }
};

class SiemensMp377AtuOutboundPrimaryHighPostErtecGuard : public SiemensMp377AtuOutboundPrimaryGuard {
public:
    using SiemensMp377AtuOutboundPrimaryGuard::SiemensMp377AtuOutboundPrimaryGuard;
    uint32_t MmioBase() const override { return siemens_mp377::kErtecBar3WindowEnd; }
    uint32_t MmioSize() const override { return kAtuPrimaryEnd - MmioBase(); }
};

/* The ATU secondary outbound window is split around MRAM, the discovered
   power/reset block at 0xD0180000..0xD0183000, and the OneNAND chip at
   0xD0200000..0xD0280000.  PeripheralDispatcher rejects overlaps. */
class SiemensMp377AtuOutboundSecondaryLowPreAspc2Guard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return kMp377MramBase + kMp377MramSize; }
    uint32_t MmioSize() const override { return kMp377Aspc2Base - MmioBase(); }
};

class SiemensMp377AtuOutboundSecondaryPostAspc2PreRamGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return kMp377Aspc2Base + kMp377Aspc2Size; }
    uint32_t MmioSize() const override { return kMp377Aspc2RamPa - MmioBase(); }
};

class SiemensMp377AtuOutboundSecondaryPostRamPrePowerGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return kMp377Aspc2RamPa + kMp377Aspc2RamSize; }
    uint32_t MmioSize() const override { return kMp377PowerResetBase - MmioBase(); }
};

class SiemensMp377AtuOutboundSecondaryLowPostPowerGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return kMp377PowerResetEnd; }
    uint32_t MmioSize() const override { return 0xD0200000u - MmioBase(); }
};
class SiemensMp377AtuOutboundSecondaryHighGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return 0xD0280000u; }   /* past chip end */
    uint32_t MmioSize() const override { return 0x03D80000u; }   /* remainder */
};

/* PA 0xFF000000+16MB band, flanking the IOP13xx PMMR at 0xFFD80000+1MB. */
class SiemensMp377FfBandLowGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return 0xFF000000u; }
    uint32_t MmioSize() const override { return 0x00D80000u; }   /* up to PMMR */
};

class SiemensMp377FfBandHighGuard : public Mp377BoardIoWindow {
public:
    using Mp377BoardIoWindow::Mp377BoardIoWindow;
    uint32_t MmioBase() const override { return 0xFFE80000u; }   /* past PMMR */
    uint32_t MmioSize() const override { return 0x0017F000u; }   /* stop before 32-bit exclusive-end wrap */
};

}  /* namespace */

REGISTER_SERVICE(SiemensMp377Ebus1LowGuard);
REGISTER_SERVICE(SiemensMp377Ebus1MidGuard);
REGISTER_SERVICE(SiemensMp377Ebus1HighGuard);
REGISTER_SERVICE(SiemensMp377AtuPrimarySm501RegsBridge);
REGISTER_SERVICE(SiemensMp377AtuPrimarySm501FbBridge);
REGISTER_SERVICE(SiemensMp377AtuOutboundPrimaryAfterRegsPreFbGuard);
REGISTER_SERVICE(SiemensMp377AtuOutboundPrimaryAfterFbPreBridgeGuard);
REGISTER_SERVICE(SiemensMp377AtuOutboundPrimaryBetweenBridgeGuard);
REGISTER_SERVICE(SiemensMp377AtuOutboundPrimaryHighPreErtecGuard);
REGISTER_SERVICE(SiemensMp377AtuOutboundPrimaryHighBetweenErtecGuard);
REGISTER_SERVICE(SiemensMp377AtuOutboundPrimaryHighPostErtecGuard);
REGISTER_SERVICE(SiemensMp377AtuOutboundSecondaryLowPreAspc2Guard);
REGISTER_SERVICE(SiemensMp377AtuOutboundSecondaryPostAspc2PreRamGuard);
REGISTER_SERVICE(SiemensMp377AtuOutboundSecondaryPostRamPrePowerGuard);
REGISTER_SERVICE(SiemensMp377AtuOutboundSecondaryLowPostPowerGuard);
REGISTER_SERVICE(SiemensMp377AtuOutboundSecondaryHighGuard);
REGISTER_SERVICE(SiemensMp377FfBandLowGuard);
REGISTER_SERVICE(SiemensMp377FfBandHighGuard);

