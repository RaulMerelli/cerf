#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

class NecMobilePro900Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        /* "NEC MobilePro 900" is the OEM device-name string embedded (UTF-16)
           in the P530 ROM; present in both the HPC2000 and CE4.2 generations
           and in no other board's ROM. */
        return RomContainsString("NEC MobilePro 900");
    }

    Board       GetBoard()  const override { return Board::NecMobilePro900; }
    SocFamily   GetSoc()    const override { return SocFamily::PXA25x; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    const char* BoardName() const override {
        return "NEC MobilePro 900 (P530), Intel XScale PXA255 (ARMv5TE)";
    }
    const char*    GetShortBoardName()  const override { return "MobilePro 900"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_NEC"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 640, 240 };
    }

    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro900Detector, BoardDetector);
