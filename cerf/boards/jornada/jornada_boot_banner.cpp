#include "../../core/cerf_emulator.h"
#include "../../host/text_mode_boot_banner.h"
#include "../board_detector.h"

#include <string>
#include <vector>

namespace {

/* HP Jornada boot logo, shared by the SA-11x0 Jornada handhelds (720 + 820). */
class JornadaBootBanner : public TextModeBootBanner {
public:
    using TextModeBootBanner::TextModeBootBanner;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        if (!bd) return false;
        const Board b = bd->GetBoard();
        return b == Board::Jornada720 || b == Board::Jornada820;
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
REGISTER_SERVICE_AS(JornadaBootBanner, TextModeBootBanner);

}  /* namespace */
