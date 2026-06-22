#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class SimpadSl4Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        /* Both SIMpad SL4 ROM generations (HPC2000 CE3 and CE .NET 4.10)
           carry the literal "SIMpad" (exact case) in the image; no other
           CERF board's ROM does (iPAQ uses "Compaq iPAQ", Jornadas use
           "Jornada"). */
        return RomContainsString("SIMpad");
    }

    Board       GetBoard()  const override { return Board::SimpadSl4; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1110; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    const char* BoardName() const override {
        return "Siemens SIMpad SL4 (Webpad), Intel SA-1110 StrongARM";
    }
    const char*    GetShortBoardName()  const override { return "SIMpad SL4"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_SIEMENS"; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{800, 600};
    }

    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SimpadSl4Detector, BoardDetector);
