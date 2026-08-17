#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class SiemensMp377Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()          const override { return Board::SiemensMP377; }
    SocFamily      GetSoc()            const override { return SocFamily::IOP13xx; }
    CpuArch        GetCpuArch()        const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensMp377Context, BoardContext);
