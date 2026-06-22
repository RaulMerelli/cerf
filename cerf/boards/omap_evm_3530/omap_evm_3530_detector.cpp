#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

/* ddi_tievm3530.dll is the ti_evm_3530 BSP's display driver and
   ships nowhere else; any ROM containing it is this BSP. */
class OmapEvm3530Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        return NameContains(ModuleNames(), "ddi_tievm3530");
    }

    Board       GetBoard()  const override { return Board::OmapEvm3530; }
    SocFamily   GetSoc()    const override { return SocFamily::OMAP3530; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    const char* BoardName() const override {
        return "TI OMAP 3530 EVM (Cortex-A8) + ti_evm_3530 BSP (CE 7)";
    }
    const char*    GetShortBoardName()  const override { return "OMAP3530 EVM"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_TI"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(OmapEvm3530Detector, BoardDetector);
