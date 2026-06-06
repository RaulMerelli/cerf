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
               RomContainsString("Compaq iPAQ H3100");
    }

    Board       GetBoard()  const override { return Board::IpaqGen1; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1110; }
    const char* BoardName() const override {
        return "Compaq iPAQ 1st gen (H31xx/H36xx), Intel SA-1110 StrongARM";
    }

};

}  /* namespace */

REGISTER_SERVICE_AS(IpaqGen1Detector, BoardDetector);
