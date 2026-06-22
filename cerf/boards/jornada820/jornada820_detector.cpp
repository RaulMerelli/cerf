#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class Jornada820Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        /* Device-identity string "HP, Jornada 820, ..." in the ROM
           (UTF-16, nk.exe @ 0x38FA8); occurs in no other CERF bundle. */
        return RomContainsString("Jornada 820");
    }

    Board       GetBoard()  const override { return Board::Jornada820; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1100; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    const char* BoardName() const override {
        return "HP Jornada 820 Handheld PC, Intel SA-1100 StrongARM";
    }
    const char*    GetShortBoardName()  const override { return "Jornada 820"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_HP"; }

    /* Fixed 640x480 VGA dual-panel STN; size the window to it pre-boot. */
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 640, 480 };
    }

    // 32bpp causes at least IE picture rendering broken
    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada820Detector, BoardDetector);
