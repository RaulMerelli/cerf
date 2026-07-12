#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class OdoArm720Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::OdoArm720; }
    SocFamily   GetSoc()    const override { return SocFamily::Poseidon; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char*    GetShortBoardName()  const override { return "Reference Platform"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_MICROSOFT"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 480, 240 };
    }
    
    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(OdoArm720Context, BoardContext);
