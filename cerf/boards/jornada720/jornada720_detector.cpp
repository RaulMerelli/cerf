#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class Jornada720Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        /* Device-identity string "Jornada 720" in the ROM (UTF-16);
           occurs in no other CERF bundle. */
        return RomContainsString("Jornada 720");
    }

    Board       GetBoard()  const override { return Board::Jornada720; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1110; }
    const char* BoardName() const override {
        return "HP Jornada 720 Handheld PC, Intel SA-1110 StrongARM";
    }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 640, 240 };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada720Detector, BoardDetector);
