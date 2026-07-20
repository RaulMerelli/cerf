#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class CasioCassiopeiaEm500Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()           const override { return Board::CasioCassiopeiaEm500; }
    SocFamily      GetSoc()             const override { return SocFamily::VR4122; }
    CpuArch        GetCpuArch()         const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode()  const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName()  const override { return "EM-500"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 240, 320 };
    }
    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }
    /* VR4131 UM Fig 3-1: PA 0x20000000-0xFFFFFFFF mirrors 0x00000000-0x1FFFFFFF. */
    uint32_t GuestAdditionsWindowBase() const override { return 0x04000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioCassiopeiaEm500Context, BoardContext);
