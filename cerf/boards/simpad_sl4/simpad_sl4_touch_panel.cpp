#include "simpad_sl4_touch_panel.h"

#include "../board_detector.h"
#include "../../core/cerf_emulator.h"

bool SimpadSl4TouchPanel::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::SimpadSl4;
}

REGISTER_SERVICE(SimpadSl4TouchPanel);
