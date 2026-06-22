#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

/* NEC Rockhopper (DDB-VR5500A): VR5500 CPU module on the SolutionGear2 (SG2)
   board, Windows CE 6, MIPS IV. Fingerprint: the board-unique "SG2_VR5500"
   platform-type token (BSP IOCTL_PLATFORM_TYPE). */
class NecRockhopperDetector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        return RomContainsString("SG2_VR5500");
    }

    Board       GetBoard()   const override { return Board::NecRockhopper; }
    SocFamily   GetSoc()     const override { return SocFamily::VR5500; }
    CpuArch     GetCpuArch() const override { return CpuArch::Mips; }
    const char* BoardName()  const override {
        return "NEC Rockhopper (DDB-VR5500A, MIPS VR5500, Windows CE 6)";
    }
    const char*    GetShortBoardName()  const override { return "Rockhopper"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_NEC"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecRockhopperDetector, BoardDetector);
