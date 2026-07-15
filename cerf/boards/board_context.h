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
    SA1100,
    PXA25x,
    PXA27x,
    OMAP3530,
    Poseidon,
    iMX31,
    iMX32,
    iMX51,
    TegraAPX,
    VR5500,
    VR4102,
    VR4121,
    PR31700,
    PR31500,
};

enum class CpuArch { Arm, Mips };

enum class RomPlacingMode { FlatContainer, Imx51Nand, Unknown };

enum class Board {
    Unknown,
    Smdk2410DevEmu,
    OdoArm720,
    OmapEvm3530,
    IpaqGen1,
    ZuneKeel,
    FalconPC3xx,
    Jornada720,
    Jornada820,
    SimpadSl4,
    NecMobilePro900,
    FordSyncGen2,
    SiemensP177,
    SmartBookG138,
    NecRockhopper,
    NecMobilePro700,
    CasioToricomail,
    PhilipsNino300,
    PhilipsVelo1,
    SharpMobilonHc4100,
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
    virtual CpuArch        GetCpuArch()         const = 0;
    virtual RomPlacingMode GetRomPlacingMode()  const = 0;

    /* Short consumer name shown on the boot screen beneath the OEM logo
       ("Starting <name>..."). Defaults to the full board name; boards
       override with a brief device name. */
    virtual const char* GetShortBoardName() const { return BoardName(GetBoard()); }

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

    virtual uint32_t GuestAdditionsWindowBase() const { return 0xF0000000u; }

    static const char* BoardName(Board b);
    static const char* SocFamilyName(SocFamily f);

    static std::span<const BoardIdEntry> BoardIds();
    static Board                         BoardFromId(const std::string& id);
};
