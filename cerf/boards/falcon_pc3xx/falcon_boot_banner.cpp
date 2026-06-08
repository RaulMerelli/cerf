#include "../../core/cerf_emulator.h"
#include "../../host/text_mode_boot_banner.h"
#include "../board_detector.h"

#include <string>
#include <vector>

namespace {

class FalconBootBanner : public TextModeBootBanner {
public:
    using TextModeBootBanner::TextModeBootBanner;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::FalconPC3xx;
    }

protected:
    std::vector<std::string> LogoLines() const override {
        return {
            "#####   ####   ###",
            "##  ## ##_   ##",
            "#####     ## ##",
            "##     ####    ###",
            "##",
        };
    }
};
REGISTER_SERVICE_AS(FalconBootBanner, TextModeBootBanner);

}  /* namespace */
