#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class ZuneKeelContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::ZuneKeel; }
    SocFamily   GetSoc()    const override { return SocFamily::iMX31; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName()  const override { return "Zune 30"; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 240, 320 };
    }
    /* gemstone renders via XUI -> Direct3D Mobile, whose device init rejects any
       display mode that is not 16bpp RGB565 (ddraw_ipu_sdc D3DM sub_34E8CDC). */
    uint32_t GetGuestAdditionsColorDepth() const override { return 16u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(ZuneKeelContext, BoardContext);
