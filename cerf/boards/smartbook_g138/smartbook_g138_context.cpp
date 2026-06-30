#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class SmartBookG138Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::SmartBookG138; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1110; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char* BoardName() const override {
        return "SmartBook G138 (webpad), Intel SA-1110 StrongARM + MediaQ MQ200";
    }
    const char* GetShortBoardName() const override { return "SmartBook G138"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_SMARTBOOK"; }

    /* 800x480 LCD (registry Drivers\Display\MQ200 CxScreen=0x320, CyScreen=0x1E0).
       Cosmetic pre-boot hint only; the live size comes from MQ200 OnLcdEnabled. */
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{800, 480};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SmartBookG138Context, BoardContext);
