#include "siemens_mp377_touch_panel.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../host/touch_input.h"
#include "../../socs/irq_controller.h"

namespace {

class SiemensMp377TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }

    void OnPenDown(int x, int y) override { UpdateTouch(x, y, true, true); }

    void OnPenMove(int x, int y) override { UpdateTouch(x, y, true, true); }

    void OnPenUp(int x, int y) override {
        const bool was_down = last_down_;
        UpdateTouch(x, y, false, was_down);
    }

    void OnCaptureLost() override {
        const bool was_down = last_down_;
        last_down_ = false;
        emu_.Get<siemens_mp377::SiemensMp377TouchPanel>().CaptureLost();
        if (was_down) emu_.Get<IrqController>().AssertIrq(siemens_mp377::SiemensMp377TouchPanel::kTouchIrqSource);
    }

private:
    void UpdateTouch(int x, int y, bool down, bool assert_irq) {
        last_down_ = down;
        emu_.Get<siemens_mp377::SiemensMp377TouchPanel>().UpdateHostPointer(x, y, down);

        /* touch.dll calls InterruptInitialize(SYSINTR 0x1B, touch_event,...).
           The P377 NK static map translates SYSINTR 0x1B to raw IRQ 0x23. */
        if (assert_irq) emu_.Get<IrqController>().AssertIrq(siemens_mp377::SiemensMp377TouchPanel::kTouchIrqSource);
    }

    bool last_down_ = false;
};

} // namespace

REGISTER_SERVICE_AS(SiemensMp377TouchInput, TouchInput);
