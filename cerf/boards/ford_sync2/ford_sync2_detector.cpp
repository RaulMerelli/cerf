#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

/* "Ford Sync GenII" is embedded in this board's OAL (nk.exe at 0x80101C48,
   UTF-16) and appears in no other ROM, so it uniquely fingerprints the board.
   This board ships as a `.sec` NAND image, so the needle lives in the
   de-chunked flash; a stray extracted NK.bin is matched via the XIP path too. */
class FordSyncGen2Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        return RomContainsString("Ford Sync GenII")
            || SecContainsString("Ford Sync GenII");
    }

    Board       GetBoard()  const override { return Board::FordSyncGen2; }
    SocFamily   GetSoc()    const override { return SocFamily::iMX51; }
    const char* BoardName() const override {
        return "Ford SYNC Gen2 APIM (i.MX51 Cortex-A8, Windows Embedded Compact)";
    }
    const char* GetShortBoardName() const override { return "Ford SYNC 2"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_FORD"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(FordSyncGen2Detector, BoardDetector);
