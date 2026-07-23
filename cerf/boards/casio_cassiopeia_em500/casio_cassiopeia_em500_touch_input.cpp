#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../host/touch_input.h"
#include "casio_cassiopeia_em500_companion.h"

namespace {

class CasioCassiopeiaEm500TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioCassiopeiaEm500;
    }

    void OnPenDown(int x, int y) override {
        emu_.Get<CasioCassiopeiaEm500Companion>().SetTouchPen(true, x, y);
    }
    void OnPenMove(int x, int y) override {
        emu_.Get<CasioCassiopeiaEm500Companion>().SetTouchPen(true, x, y);
    }
    void OnPenUp(int x, int y) override {
        emu_.Get<CasioCassiopeiaEm500Companion>().SetTouchPen(false, x, y);
    }
    void OnCaptureLost() override {
        emu_.Get<CasioCassiopeiaEm500Companion>().TouchCaptureLost();
    }
};

}

REGISTER_SERVICE_AS(CasioCassiopeiaEm500TouchInput, TouchInput);
