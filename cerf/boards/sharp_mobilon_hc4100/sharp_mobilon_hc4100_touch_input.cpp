#include "../../host/touch_input.h"

#include "sharp_mobilon_hc4100_touch_panel.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"

namespace {

class SharpMobilonHc4100TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
    }

    void OnPenDown(int x, int y) override {
        emu_.Get<SharpMobilonHc4100TouchPanel>().SetPen(true, x, y);
    }
    void OnPenMove(int x, int y) override {
        auto& p = emu_.Get<SharpMobilonHc4100TouchPanel>();
        if (p.Down()) p.SetPen(true, x, y);
    }
    void OnPenUp(int x, int y) override {
        emu_.Get<SharpMobilonHc4100TouchPanel>().SetPen(false, x, y);
    }
    void OnCaptureLost() override {
        emu_.Get<SharpMobilonHc4100TouchPanel>().SetPen(false, 0, 0);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHc4100TouchInput, TouchInput);
