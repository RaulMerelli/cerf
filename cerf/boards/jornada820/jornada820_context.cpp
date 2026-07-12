#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class Jornada820Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::Jornada820; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1100; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName()  const override { return "Jornada 820"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_HP"; }

    /* Fixed 640x480 VGA dual-panel STN; size the window to it pre-boot. */
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 640, 480 };
    }

    // 32bpp causes at least IE picture rendering broken
    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada820Context, BoardContext);
