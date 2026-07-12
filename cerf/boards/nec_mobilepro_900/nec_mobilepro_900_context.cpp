#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class NecMobilePro900Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::NecMobilePro900; }
    SocFamily   GetSoc()    const override { return SocFamily::PXA25x; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName()  const override { return "MobilePro 900"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_NEC"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 640, 240 };
    }

    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro900Context, BoardContext);
