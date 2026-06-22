#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class IpaqGen1Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        /* H36xx ROMs carry only "Compaq iPAQ H3600" (nk.exe 0x80054850,
           never "H3650"); H31xx ROMs carry only "Compaq iPAQ H3100".
           Dropping/narrowing either needle un-detects that ROM family. */
        return RomContainsString("Compaq iPAQ H3600") ||
               RomContainsString("Compaq iPAQ H3100") || 
               RomContainsString("Compaq iPAQ H3700");
    }

    Board       GetBoard()  const override { return Board::IpaqGen1; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1110; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    const char* BoardName() const override {
        return "Compaq iPAQ 1st gen (H31xx/H36xx), Intel SA-1110 StrongARM";
    }
    const char*    GetShortBoardName()  const override { return "iPAQ"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_COMPAQ"; }
    /* CE3 imgdecmp DecompressImageIndirect rejects bpp not in {2,4,8,16,24}
       (imgdecmp.dll 0x1124300) and blanks the shell bitmaps at 32bpp. */
    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{240u, 320u};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(IpaqGen1Detector, BoardDetector);
