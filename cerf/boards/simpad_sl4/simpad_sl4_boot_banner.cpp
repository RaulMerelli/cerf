#include "../../core/cerf_emulator.h"
#include "../../host/text_mode_boot_banner.h"
#include "../board_detector.h"

#include <string>
#include <vector>

namespace {

/* Siemens SIMpad SL4 ("Webpad") boot logo. */
class SimpadSl4BootBanner : public TextModeBootBanner {
public:
    using TextModeBootBanner::TextModeBootBanner;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

protected:
    std::vector<std::string> LogoLines() const override {
        return {
            "##### ##### ##### #   # ##### #   # #####",
            "#       #   #     ## ## #     ##  # #",
            "#####   #   ##### # # # ##### # # # #####",
            "    #   #   #     #   # #     #  ##     #",
            "##### ##### ##### #   # ##### #   # #####",
        };
    }
};
REGISTER_SERVICE_AS(SimpadSl4BootBanner, TextModeBootBanner);

}  /* namespace */
