#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class PhilipsVelo1Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()          const override { return Board::PhilipsVelo1; }
    SocFamily      GetSoc()            const override { return SocFamily::PR31500; }
    CpuArch        GetCpuArch()        const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName() const override { return "Velo 1"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_PHILIPS"; }

    /* 480x240 landscape (Philips Velo 1 panel). */
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{480, 240};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1Context, BoardContext);
