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
    const char* BoardName() const override {
        return "Siemens SIMpad SL4 (Webpad), Intel SA-1110 StrongARM";
    }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{800, 600};
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SimpadSl4Detector, BoardDetector);
