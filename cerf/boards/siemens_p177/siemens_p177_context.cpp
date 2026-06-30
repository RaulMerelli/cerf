#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class SiemensP177Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::SiemensP177; }
    SocFamily   GetSoc()    const override { return SocFamily::S3C2410; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char* BoardName() const override {
        return "Siemens SIMATIC TP177B 4\" (S3C2410, P177 BSP)";
    }
    const char*    GetShortBoardName()  const override { return "SIMATIC TP177B"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_SIEMENS"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{480u, 272u};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensP177Context, BoardContext);
