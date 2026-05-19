#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

/* DeviceEmulator_lcd.dll is the DeviceEmulator BSP's display driver
   and ships nowhere else; raw-byte scan rather than module-name scan
   because WM6+ NB0 images put driver DLL names in IMGFS, which
   RomParser does not parse. */
class Smdk2410DevEmuDetector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        return RomContainsString("DeviceEmulator_lcd");
    }

    Board       GetBoard()  const override { return Board::Smdk2410DevEmu; }
    SocFamily   GetSoc()    const override { return SocFamily::S3C2410; }
    const char* BoardName() const override {
        return "SMDK2410 + Microsoft DeviceEmulator BSP";
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Smdk2410DevEmuDetector, BoardDetector);
