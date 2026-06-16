#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

/* "\platform\P177\target" is a PDB-path substring baked into every retail
   binary built from the Siemens P177 BSP (e.g. nk.exe's debug symbol path
   reads c:\WINCE500\platform\P177\target\ARMV4I\retail\kern.pdb). It is
   unique to the P177 family of HMI panels and absent from every other ROM. */
class SiemensP177Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        return RomContainsString(R"(\platform\P177\target)");
    }

    Board       GetBoard()  const override { return Board::SiemensP177; }
    SocFamily   GetSoc()    const override { return SocFamily::S3C2410; }
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

REGISTER_SERVICE_AS(SiemensP177Detector, BoardDetector);
