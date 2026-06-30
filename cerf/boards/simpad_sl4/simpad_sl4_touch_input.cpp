#include "../../host/touch_input.h"

#include "simpad_sl4_touch_panel.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"

namespace {

/* Routes host pen events into the shared SIMpad pen state; the UCB1300 codec
   reads that state to answer touch.dll's ADC reads over the SA-1110 MCP. */
class SimpadSl4TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

    void OnPenDown(int x, int y) override {
        emu_.Get<SimpadSl4TouchPanel>().SetPen(true, x, y);
    }
    void OnPenMove(int x, int y) override {
        auto& p = emu_.Get<SimpadSl4TouchPanel>();
        if (p.Down()) p.SetPen(true, x, y);
    }
    void OnPenUp(int x, int y) override {
        emu_.Get<SimpadSl4TouchPanel>().SetPen(false, x, y);
    }
    void OnCaptureLost() override {
        emu_.Get<SimpadSl4TouchPanel>().SetPen(false, 0, 0);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SimpadSl4TouchInput, TouchInput);
