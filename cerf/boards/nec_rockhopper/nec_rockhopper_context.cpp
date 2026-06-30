#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

/* NEC Rockhopper (DDB-VR5500A): VR5500 CPU module on the SolutionGear2 (SG2)
   board, Windows CE 6, MIPS IV. */
class NecRockhopperContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()   const override { return Board::NecRockhopper; }
    SocFamily   GetSoc()     const override { return SocFamily::VR5500; }
    CpuArch     GetCpuArch() const override { return CpuArch::Mips; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char* BoardName()  const override {
        return "NEC Rockhopper (DDB-VR5500A, MIPS VR5500, Windows CE 6)";
    }
    const char*    GetShortBoardName()  const override { return "Rockhopper"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_NEC"; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{640u, 480u};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecRockhopperContext, BoardContext);
