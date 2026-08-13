#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class CasioCassiopeiaE55Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()           const override { return Board::CasioCassiopeiaE55; }
    SocFamily      GetSoc()             const override { return SocFamily::VR4111; }
    CpuArch        GetCpuArch()         const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode()  const override { return RomPlacingMode::FlatContainer; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 240, 320 };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioCassiopeiaE55Context, BoardContext);
