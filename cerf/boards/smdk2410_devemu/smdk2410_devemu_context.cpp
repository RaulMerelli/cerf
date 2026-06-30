#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class Smdk2410DevEmuContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::Smdk2410DevEmu; }
    SocFamily   GetSoc()    const override { return SocFamily::S3C2410; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }
    const char* BoardName() const override {
        return "SMDK2410 + Microsoft DeviceEmulator BSP";
    }
    const char*    GetShortBoardName()  const override { return "Device Emulator"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_MICROSOFT"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Smdk2410DevEmuContext, BoardContext);
