#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class FalconPc3xxDetector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        /* xsc1bd_serial.dll = Datalogic XScale serial driver; BCDCore.dll
           = the PSC/Datalogic barcode-decoder core. Both modules are
           unique to the Falcon barcode-terminal ROM. */
        const std::string names = ModuleNames();
        return NameContains(names, "xsc1bd_serial") &&
               NameContains(names, "BCDCore");
    }

    Board       GetBoard()  const override { return Board::FalconPC3xx; }
    SocFamily   GetSoc()    const override { return SocFamily::PXA25x; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    const char* BoardName() const override {
        return "Datalogic Falcon 4220 (PC3xx), "
               "Intel XScale PXA255 (ARMv5TE), Windows CE .NET 4.2";
    }
    const char*    GetShortBoardName()  const override { return "Falcon 4220"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_PSC"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 240, 320 };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(FalconPc3xxDetector, BoardDetector);
