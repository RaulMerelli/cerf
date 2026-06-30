#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class FordSyncGen2Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::FordSyncGen2; }
    SocFamily   GetSoc()    const override { return SocFamily::iMX51; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::Imx51Nand; }
    const char* BoardName() const override {
        return "Ford SYNC Gen2 APIM (i.MX51 Cortex-A8, Windows Embedded Compact)";
    }
    const char* GetShortBoardName() const override { return "Ford SYNC 2"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_FORD"; }

    /* Pre-boot hint only; real sizing comes from OnLcdEnabled. The SYNC2 8" panel
       is 800x480 (runtime IPU CPMEM ch23 scanout: 800x480 RGB565 @ 0x90E34000). */
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{800u, 480u};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(FordSyncGen2Context, BoardContext);
