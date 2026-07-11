#include "../../host/touch_input.h"

#include "philips_velo_1_touch_panel.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"

namespace {

class PhilipsVelo1TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsVelo1;
    }

    void OnPenDown(int x, int y) override {
        emu_.Get<PhilipsVelo1TouchPanel>().SetPen(true, x, y);
    }
    void OnPenMove(int x, int y) override {
        auto& p = emu_.Get<PhilipsVelo1TouchPanel>();
        if (p.Down()) p.SetPen(true, x, y);
    }
    void OnPenUp(int x, int y) override {
        emu_.Get<PhilipsVelo1TouchPanel>().SetPen(false, x, y);
    }
    void OnCaptureLost() override {
        emu_.Get<PhilipsVelo1TouchPanel>().SetPen(false, 0, 0);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1TouchInput, TouchInput);
