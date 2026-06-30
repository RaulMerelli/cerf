#pragma once

#include "../core/service.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

enum class SocFamily {
    Unknown,
    S3C2410,
    SA1110,
    SA1100,    /* Intel SA-1100 StrongARM, ARMv4. Same peripheral silicon as
                  SA-1110 (shares cerf/socs/sa11xx + cerf/cpu/sa11xx); differs
                  only in CPU ID (proc-sa1100.S:276 = 0x4401a110). */
    PXA25x,    /* Intel XScale PXA255 ("Cotulla"), ARMv5TE */
    PXA27x,
    OMAP3530,
    Poseidon,
    iMX31,
    iMX32,
    iMX51,     /* Freescale i.MX51 (ARM Cortex-A8, ARMv7-A). IPUv3 display,
                  TZIC interrupt controller, MC13892 PMIC. Confirmed from the
                  ROM's OAL: BSP source path MX51_FSL_V2 + "i.MX51" string +
                  silicon-register init in nk.exe start() (0x80101000). */
    TegraAPX,
    VR5500,    /* NEC VR5500, MIPS IV core (64-bit silicon, soft-float CE build),
                  little-endian. CERF's first non-ARM guest ISA. VR5500 CPU module
                  on the SolutionGear2 (SG2) board with the ALi M1535+ southbridge. */
};

enum class CpuArch { Arm, Mips };

enum class RomPlacingMode { FlatContainer, Imx51Nand, Unknown };

enum class Board {
    Unknown,
    Smdk2410DevEmu,   /* Microsoft DeviceEmulator BSP on Samsung SMDK2410 */
    OdoArm720,        /* Microsoft Odo CE3 reference platform + Philips
                         Poseidon peripheral ASIC, ARM720T CPU socket; BSP
                         builds Thumb (_TGTCPUTYPE=THUMB, _TGTCPU=ARM720). */
    OmapEvm3530,      /* TI OMAP 3530 EVM (Cortex-A8 / CE7); EVM1 and EVM2 ship same BSP */
    IpaqGen1,         /* Compaq iPAQ first generation (H31xx mono, H36xx color,
                         H37xx SKUs - EGPIO + Atmel MicroP companions), Intel
                         SA-1110. One BSP across the family; H38xx (ASIC1/2)
                         and H39xx (PXA, ASIC3) are separate future boards. */
    ZuneKeel,         /* Microsoft Zune 30, board codename Keel, Freescale
                         i.MX31L (ARM1136JF-S, ARMv6K + VFPv2). */
    FalconPC3xx,      /* Datalogic Falcon 4220 rugged handheld, PC3xx board,
                         Intel XScale PXA255 (ARMv5TE), Windows CE .NET 4.2. */
    Jornada720,       /* HP Jornada 720 Handheld PC, Intel SA-1110 StrongARM
                         + SA-1111 companion + EPSON SED1356 LCD + SSP MCU.
                         Windows CE 3.0 / HPC2000. */
    Jornada820,       /* HP Jornada 820 Handheld PC, Intel SA-1100 StrongARM,
                         integrated SA-1100 LCD controller (no SED1356, no
                         SA-1111), keyboard + glidepad touchpad. Windows CE
                         2.11 / Handheld PC Professional Edition. */
    SimpadSl4,        /* Siemens SIMpad SL4 ("Webpad"), Intel SA-1110 StrongARM.
                         One board, two ROM generations: HPC2000 (CE 3.0) and
                         Windows CE .NET 4.10. CE image physfirst=0x80080000. */
    NecMobilePro900,  /* NEC MobilePro 900 / 900c (OEM board codename "P530"),
                         Intel XScale PXA255 (ARMv5TE), 640x240 DSTN, QWERTY HPC
                         clamshell. Two ROM generations: HPC2000 (CE 3.0) and
                         Windows CE .NET 4.2. */
    FordSyncGen2,     /* Ford SYNC Generation II (APIM, board id EA5T-14D544-BA),
                         Freescale i.MX51 (ARM Cortex-A8), Windows Embedded
                         Compact. Automotive head unit. */
    SiemensP177,      /* Siemens SIMATIC TP177B 4" HMI panel, Samsung S3C2410
                         (ARM920T), Windows CE 5.0 / Siemens P177 BSP. */
    SmartBookG138,    /* SmartBook G138 webpad, Intel SA-1110 StrongARM + MediaQ
                         MQ200 display, Windows CE .NET 4.x (4.1 + 4.2 ROMs). */
    NecRockhopper,    /* NEC Rockhopper (DDB-VR5500A): NEC VR5500 CPU module on the
                         SolutionGear2 (SG2) motherboard, ALi M1535+ southbridge,
                         Windows CE 6, MIPS IV. */
};

/* A board's fixed host-window open size, in guest-surface pixels. */
struct PreferredWindowSize { uint32_t width; uint32_t height; };

struct BoardIdEntry { const char* id; Board board; };

class BoardContext : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    virtual Board          GetBoard()          const = 0;
    virtual SocFamily      GetSoc()             const = 0;
    virtual const char*    BoardName()          const = 0;
    virtual CpuArch        GetCpuArch()         const = 0;
    virtual RomPlacingMode GetRomPlacingMode()  const = 0;

    /* Short consumer name shown on the boot screen beneath the OEM logo
       ("Starting <name>..."). Defaults to the full BoardName(); boards
       override with a brief device name. */
    virtual const char* GetShortBoardName() const { return BoardName(); }

    /* RT_RCDATA resource name of the board's OEM boot logo (a PNG embedded via
       cerf.rc). Defaults to the generic Windows CE logo; boards override with
       their own. BootScreen owns decoding; the context stays free of any
       GDI/Win32 dependency (a wchar_t* resource name needs no windows.h). */
    virtual const wchar_t* GetBootLogoResource() const { return L"OEM_WINCE"; }

    /* Cosmetic pre-boot window-size hint for boards with a single fixed LCD.
       Never route actual sizing through this - the real resolution comes
       solely from OnLcdEnabled, and overriding that here would ignore what
       the guest LCD reports. nullopt (base default) = no hint. */
    virtual std::optional<PreferredWindowSize> GetPreferredWindowSize() const {
        return std::nullopt;
    }

    /* Guest-Additions framebuffer colour depth in bpp (the cerf_virt FB format
       the guest's display/DDraw stack sees). Base = 32 (host-native BGRA). A
       board overrides when its guest software hard-requires another depth - Zune
       XUI/D3-Mobile only accepts 16bpp RGB565; CE3 imgdecmp rejects 32bpp. */
    virtual uint32_t GetGuestAdditionsColorDepth() const { return 32u; }

    static const char* SocFamilyName(SocFamily f);

    static std::span<const BoardIdEntry> BoardIds();
    static Board                         BoardFromId(const std::string& id);
};
