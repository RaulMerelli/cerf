#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class HmiKtp400MobileContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()          const override { return Board::HmiKtp400Mobile; }
    SocFamily      GetSoc()            const override { return SocFamily::iMX6; }
    CpuArch        GetCpuArch()        const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{480u, 272u};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(HmiKtp400MobileContext, BoardContext);
