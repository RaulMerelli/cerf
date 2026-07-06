#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

/* NEC MobilePro 700 Series: NEC VR4102 (VR4100 core, MIPS), Handheld PC
   clamshell, Windows CE 2.0 (1997). Flat NB0 XIP. */
class NecMobilePro700Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board          GetBoard()          const override { return Board::NecMobilePro700; }
    SocFamily      GetSoc()            const override { return SocFamily::VR4102; }
    CpuArch        GetCpuArch()        const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    BoardName()         const override {
        return "NEC MobilePro 700 Series (NEC VR4102, MIPS, Windows CE 2.0)";
    }
    const char*    GetShortBoardName()  const override { return "MobilePro 700"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_NEC"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro700Context, BoardContext);
