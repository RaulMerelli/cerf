#include "../../socs/vr41xx_piu_panel.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_context.h"

#include <cstdint>
#include <optional>

namespace {

/* PIUCMDREG ADCMD(3:0) = 0000 selects the TPX0 port (VR4121 UM 20.3.5 (2/2)). touch.dll
   sub_13715A0 issues exactly one command scan, with PIUCMDREG = 0xCC0 (TPYEN = 11,
   TPYD = 11, ADCMD = 0000), and no module in the ROM reads PIUAB0REG. */
constexpr uint16_t kAdcmdTpx0 = 0;

class CasioToricomailTouchPanel : public Vr41xxPiuPanel {
public:
    using Vr41xxPiuPanel::Vr41xxPiuPanel;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioToricomail;
    }

    std::optional<uint16_t> ConvertCommandPort(uint16_t adcmd,
                                               uint16_t /*pos_x*/,
                                               uint16_t /*pos_y*/) override {
        if (adcmd != kAdcmdTpx0) {
            LOG(Caution, "CasioToricomailTouchPanel: PIUCMDREG ADCMD=0x%X selects an A/D "
                    "port this board's panel does not drive\n", adcmd);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        return std::nullopt;
    }

    /* PIUCNTREG D5 PADSCANTYPE is "touch pressure sampling enable - 0: Prohibit" (VR4121 UM
       20.3.1); touch.dll sub_13715A0 never sets it, and sub_1370AB0 reads only PIUPBn0-3. */
    std::optional<uint16_t> PressureSample() override { return std::nullopt; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioToricomailTouchPanel, Vr41xxPiuPanel);
