#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class FalconPc3xxContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::FalconPC3xx; }
    SocFamily   GetSoc()    const override { return SocFamily::PXA25x; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName()  const override { return "Falcon 4220"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_PSC"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 240, 320 };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(FalconPc3xxContext, BoardContext);
