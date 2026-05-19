#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class OdoArm720Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        return ContainsString(
            ReadKernelBlob(),
            "OEMEthInit: Error reading IP config from SMC EEPROM");
    }

    Board       GetBoard()  const override { return Board::OdoArm720; }
    SocFamily   GetSoc()    const override { return SocFamily::Poseidon; }
    const char* BoardName() const override {
        return "Microsoft Odo CE3 reference + Philips Poseidon ASIC, ARM720T";
    }

};

}  /* namespace */

REGISTER_SERVICE_AS(OdoArm720Detector, BoardDetector);
