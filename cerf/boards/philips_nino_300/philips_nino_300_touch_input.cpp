#include "../../host/touch_input.h"

#include "philips_nino_300_touch_panel.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"

namespace {

class PhilipsNino300TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsNino300;
    }

    void OnPenDown(int x, int y) override {
        emu_.Get<PhilipsNino300TouchPanel>().SetPen(true, x, y);
    }
    void OnPenMove(int x, int y) override {
        auto& p = emu_.Get<PhilipsNino300TouchPanel>();
        if (p.Down()) p.SetPen(true, x, y);
    }
    void OnPenUp(int x, int y) override {
        emu_.Get<PhilipsNino300TouchPanel>().SetPen(false, x, y);
    }
    void OnCaptureLost() override {
        emu_.Get<PhilipsNino300TouchPanel>().SetPen(false, 0, 0);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsNino300TouchInput, TouchInput);
