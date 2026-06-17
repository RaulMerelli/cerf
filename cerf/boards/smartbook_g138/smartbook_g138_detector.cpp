#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class SmartBookG138Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        /* Both CE .NET 4.1 and 4.2 ROMs bake the OEM build path
           "E:\138_PR~1\G138_...\WINCE4xx\"; the model token "G138" is the
           board-unique, version-independent part. No other CERF board's ROM
           carries "G138". */
        return RomContainsString("G138");
    }

    Board       GetBoard()  const override { return Board::SmartBookG138; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1110; }
    const char* BoardName() const override {
        return "SmartBook G138 (webpad), Intel SA-1110 StrongARM + MediaQ MQ200";
    }
    const char* GetShortBoardName() const override { return "SmartBook G138"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_SMARTBOOK"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SmartBookG138Detector, BoardDetector);
