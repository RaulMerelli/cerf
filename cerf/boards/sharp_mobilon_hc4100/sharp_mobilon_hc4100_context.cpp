#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class SharpMobilonHc4100Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()          const override { return Board::SharpMobilonHc4100; }
    SocFamily      GetSoc()            const override { return SocFamily::PR31700; }
    CpuArch        GetCpuArch()        const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName() const override { return "Mobilon HC-4100"; }
    /* 6.5" 640x240 MSTN landscape panel (PhoneDB id=235 / hpcfactor device 151). */
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{640, 240};
    }

    uint32_t GuestAdditionsWindowBase() const override { return 0x20000000u; }

    uint32_t GetGuestAdditionsColorDepth() const override { return 8u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHc4100Context, BoardContext);
