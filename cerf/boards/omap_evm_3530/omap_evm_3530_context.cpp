#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class OmapEvm3530Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::OmapEvm3530; }
    SocFamily   GetSoc()    const override { return SocFamily::OMAP3530; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char* BoardName() const override {
        return "TI OMAP 3530 EVM (Cortex-A8) + ti_evm_3530 BSP (CE 7)";
    }
    const char*    GetShortBoardName()  const override { return "OMAP3530 EVM"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_TI"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(OmapEvm3530Context, BoardContext);
