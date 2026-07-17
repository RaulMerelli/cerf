#define NOMINMAX

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../host/host_canvas.h"
#include "../../host/touch_input.h"
#include "vr41xx_piu.h"

#include <cstdint>

namespace {

/* PADDATA(9:0) is the A/D converter's 10-bit sampling data (VR4121 UM 20.3.9,
   VR4102 UM 19.3.9). */
constexpr uint16_t kAdcMax = 1023u;

uint16_t MapAxis(int v, int v_max) {
    if (v_max <= 0 || v <= 0) return 0;
    if (v >= v_max) return kAdcMax;
    return static_cast<uint16_t>(static_cast<uint32_t>(v) * kAdcMax /
                                 static_cast<uint32_t>(v_max));
}

class Vr41xxPiuTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::VR4102 || soc == SocFamily::VR4121;
    }

    void OnPenDown(int x, int y) override { Drive(true, x, y); }
    void OnPenMove(int x, int y) override { Drive(true, x, y); }
    void OnPenUp  (int x, int y) override { Drive(false, x, y); }
    void OnCaptureLost() override {
        emu_.Get<Vr41xxPiu>().SetPen(false, last_x_, last_y_);
    }

private:
    void Drive(bool down, int hx, int hy) {
        auto& hc = emu_.Get<HostCanvas>();
        last_x_ = MapAxis(hx, static_cast<int>(hc.GuestSurfaceWidth())  - 1);
        last_y_ = MapAxis(hy, static_cast<int>(hc.GuestSurfaceHeight()) - 1);
        emu_.Get<Vr41xxPiu>().SetPen(down, last_x_, last_y_);
    }

    uint16_t last_x_ = 0;
    uint16_t last_y_ = 0;
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr41xxPiuTouchInput, TouchInput);
