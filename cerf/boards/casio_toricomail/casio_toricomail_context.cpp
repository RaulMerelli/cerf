#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class CasioToricomailContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()           const override { return Board::CasioToricomail; }
    SocFamily      GetSoc()             const override { return SocFamily::VR4121; }
    CpuArch        GetCpuArch()         const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode()  const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName()  const override { return "Toricomail"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 320, 240 };
    }

    /* VR4121 UM Table 6-6 (p.172): PA above 0x1FFFFFFF mirrors into
       0x00000000-0x1FFFFFFF. */
    uint32_t GuestAdditionsWindowBase() const override { return 0x04000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioToricomailContext, BoardContext);
