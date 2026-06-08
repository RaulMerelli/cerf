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
    const char* BoardName() const override {
        return "HP Jornada 820 Handheld PC, Intel SA-1100 StrongARM";
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada820Detector, BoardDetector);
