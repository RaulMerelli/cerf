#include "siemens_mp377_touch_panel.h"

#include "../board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../host/pointer_input.h"
#include "../../socs/irq_controller.h"

namespace {

class SiemensMp377TouchPointerInput : public PointerInput {
public:
    using PointerInput::PointerInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }

    void OnMove(int sx, int sy, uint32_t button_mask) override {
        const bool down = button_mask != 0u;
        const bool state_changed = down != last_down_;

        siemens_mp377::Mp377TouchUpdateHostPointer(sx, sy, down);

        /* touch.dll calls InterruptInitialize(SYSINTR 0x1B, touch_event,...).
           The P377 NK static map translates SYSINTR 0x1B to raw IRQ 0x23. */
        if (down || state_changed)
            emu_.Get<IrqController>().AssertIrq(siemens_mp377::kMp377TouchIrqSource);

        last_down_ = down;
    }
    void OnWheel(int, int, int) override {}
    void OnCaptureLost() override {
        const bool was_down = last_down_;
        last_down_ = false;
        siemens_mp377::Mp377TouchCaptureLost();
        if (was_down)
            emu_.Get<IrqController>().AssertIrq(siemens_mp377::kMp377TouchIrqSource);
    }

private:
    bool last_down_ = false;
};

} // namespace

REGISTER_SERVICE_AS(SiemensMp377TouchPointerInput, PointerInput);
