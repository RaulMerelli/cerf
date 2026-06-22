#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class ZuneKeelDetector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        return NameContains(ModuleNames(), "pyxis_keybd");
    }

    Board       GetBoard()  const override { return Board::ZuneKeel; }
    SocFamily   GetSoc()    const override { return SocFamily::iMX31; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    const char* BoardName() const override {
        return "Microsoft Zune 30 (codename Keel), "
               "Freescale i.MX31L (ARM1136JF-S)";
    }
    const char*    GetShortBoardName()  const override { return "Zune 30"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_ZUNE"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 240, 320 };
    }
    /* gemstone renders via XUI -> Direct3D Mobile, whose device init rejects any
       display mode that is not 16bpp RGB565 (ddraw_ipu_sdc D3DM sub_34E8CDC). */
    uint32_t GetGuestAdditionsColorDepth() const override { return 16u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(ZuneKeelDetector, BoardDetector);
