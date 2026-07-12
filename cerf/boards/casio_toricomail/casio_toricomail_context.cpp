#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class CasioToricomailContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()          const override { return Board::CasioToricomail; }
    SocFamily      GetSoc()            const override { return SocFamily::VR4121; }
    CpuArch        GetCpuArch()        const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName() const override { return "Toricomail"; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 320, 240 };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioToricomailContext, BoardContext);
