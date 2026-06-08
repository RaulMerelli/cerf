#include "../../core/cerf_emulator.h"
#include "../../host/text_mode_boot_banner.h"
#include "../board_detector.h"

#include <string>
#include <vector>

namespace {

class Jornada720BootBanner : public TextModeBootBanner {
public:
    using TextModeBootBanner::TextModeBootBanner;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }

protected:
    std::vector<std::string> LogoLines() const override {
        return {
            "    ##         ",
            "   ##          ",
            "  #####   #### ",
            " ##  ##  ##  ##",
            "##  ##  #####  ",
            "       ##",
            "      ##",
        };
    }
};
REGISTER_SERVICE_AS(Jornada720BootBanner, TextModeBootBanner);

}  /* namespace */
