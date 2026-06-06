#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class Jornada720Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        /* hpvgaout.dll + HPIrDA.dll each occur once in the 32MB flash and
           on no other CERF-supported board's ROM. */
        return RomContainsString("hpvgaout.dll")
            && RomContainsString("HPIrDA.dll");
    }

    Board       GetBoard()  const override { return Board::Jornada720; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1110; }
    const char* BoardName() const override {
        return "HP Jornada 720 Handheld PC, Intel SA-1110 StrongARM";
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada720Detector, BoardDetector);
