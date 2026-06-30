#include "board_context.h"

#include "../core/cerf_emulator.h"

namespace {

class NullBoardContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::Unknown; }
    SocFamily   GetSoc()    const override { return SocFamily::Unknown; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::Unknown; }
    const char* BoardName() const override { return "Unknown / unsupported"; }
};

}  /* namespace */

REGISTER_SERVICE_AS_FALLBACK(NullBoardContext, BoardContext);
