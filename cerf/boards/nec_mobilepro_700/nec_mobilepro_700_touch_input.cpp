#define NOMINMAX

#include "../../core/cerf_emulator.h"
#include "../../host/host_canvas.h"
#include "../../host/touch_input.h"
#include "../../socs/vr4102/vr4102_piu.h"
#include "../board_context.h"

#include <cstdint>

namespace {

/* Virtual resistive panel: normalize the host surface point into the PIU's
   10-bit A/D range [0,1023]. touch.dll's affine calibration (align-screen
   applet) adapts to whatever linear extents are produced, so no fixed panel
   geometry is baked in. */
uint16_t MapAxis(int v, int v_max) {
    if (v_max <= 0 || v <= 0) return 0;
    if (v >= v_max) return 1023;
    return static_cast<uint16_t>(static_cast<uint32_t>(v) * 1023u / static_cast<uint32_t>(v_max));
}

class NecMobilePro700TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro700;
    }

    void OnPenDown(int x, int y) override { Drive(true, x, y); }
    void OnPenMove(int x, int y) override { Drive(true, x, y); }
    void OnPenUp  (int x, int y) override { Drive(false, x, y); }
    void OnCaptureLost() override {
        emu_.Get<Vr4102Piu>().SetPen(false, last_x_, last_y_);
    }

private:
    void Drive(bool down, int hx, int hy) {
        auto& hc = emu_.Get<HostCanvas>();
        last_x_ = MapAxis(hx, static_cast<int>(hc.GuestSurfaceWidth())  - 1);
        last_y_ = MapAxis(hy, static_cast<int>(hc.GuestSurfaceHeight()) - 1);
        emu_.Get<Vr4102Piu>().SetPen(down, last_x_, last_y_);
    }

    uint16_t last_x_ = 0;
    uint16_t last_y_ = 0;
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro700TouchInput, TouchInput);
