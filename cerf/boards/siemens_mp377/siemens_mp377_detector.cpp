#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

/* The P377 BSP path is present in the Siemens kernel's PDB strings. The same
   ROM supports the 12", 15", and 19" MP377 variants, so display size is left
   to the guest display driver rather than guessed by the detector. */
class SiemensMp377Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        return RomContainsString(R"(\platform\P377\target)");
    }

    Board       GetBoard()  const override { return Board::SiemensMP377; }
    SocFamily   GetSoc()    const override { return SocFamily::IOP13xx; }
    const char* BoardName() const override {
        return "Siemens SIMATIC MP 377 (Intel IOP13xx, P377 BSP)";
    }
    const char*    GetShortBoardName() const override { return "SIMATIC MP 377"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_SIEMENS"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensMp377Detector, BoardDetector);
