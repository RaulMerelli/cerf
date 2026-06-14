#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

namespace {

/* Fingerprint string is unique to the Odo SMC91C94 HAL (HALETHER.C).
   Changing it without proving uniqueness across the CE3 ROM set risks
   false positives on other CE3 ROMs. */
class OdoArm720Detector : public BoardDetector {
public:
    using BoardDetector::BoardDetector;

    bool ShouldRegister() override {
        const auto blob = ReadKernelBlob();
        return ContainsString(blob.data(), blob.size(),
            "OEMEthInit: Error reading IP config from SMC EEPROM");
    }

    Board       GetBoard()  const override { return Board::OdoArm720; }
    SocFamily   GetSoc()    const override { return SocFamily::Poseidon; }
    const char* BoardName() const override {
        return "Microsoft Windows CE Hardware Reference Platform, Philips Poseidon ASIC, ARM720T";
    }
    const char*    GetShortBoardName()  const override { return "Reference Platform"; }
    const wchar_t* GetBootLogoResource() const override { return L"OEM_MICROSOFT"; }
    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{ 480, 240 };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(OdoArm720Detector, BoardDetector);
