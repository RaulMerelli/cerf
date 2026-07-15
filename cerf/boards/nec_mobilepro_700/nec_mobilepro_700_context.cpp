#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

/* NEC MobilePro 700 Series: NEC VR4102 (VR4100 core, MIPS), Handheld PC
   clamshell, Windows CE 2.0 (1997). Flat NB0 XIP. */
class NecMobilePro700Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()          const override { return Board::NecMobilePro700; }
    SocFamily      GetSoc()            const override { return SocFamily::VR4102; }
    CpuArch        GetCpuArch()        const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName()  const override { return "MobilePro 700"; }
    /* VR4102 "reserved for future use" span, UM Table 5-6: 0x04000000-0x09FFFFFF (96 MB). */
    uint32_t GuestAdditionsWindowBase() const override { return 0x04000000u; }
    /* CE2.0 gwes creates only PAL_INDEXED palettes, so the device is 8bpp indexed. */
    uint32_t GetGuestAdditionsColorDepth() const override { return 8u; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 640, 240 };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro700Context, BoardContext);
