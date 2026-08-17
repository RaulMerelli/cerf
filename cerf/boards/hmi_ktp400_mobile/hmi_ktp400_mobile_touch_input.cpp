#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../host/touch_input.h"
#include "../../peripherals/ti_tsc2017/ti_tsc2017_host_state.h"

#include <algorithm>

namespace {

uint16_t Clamp12(double v) {
    if (v < 0.0) v = 0.0;
    const uint32_t iv = static_cast<uint32_t>(v + 0.5);
    return static_cast<uint16_t>(std::min<uint32_t>(iv, 0x0FFFu));
}

/* raw = A^-1(screen). A = tchcaldll!TchCal_SetCalibrationData least-squares fit
   of the 5 registry CalibrationData raw points (default.hv
   HKLM\HARDWARE\DEVICEMAP\TOUCH) to tchproxy!TouchPanelGetDeviceCaps default
   crosshairs (center + W/5 insets, 480x272). Recompute if either changes. */
void ScreenToRaw(double sx, double sy, uint16_t& raw_x, uint16_t& raw_y) {
    raw_x = Clamp12(7.95676985 * sx + 0.02794286 * sy + 92.37500677);
    raw_y = Clamp12(0.06745084 * sx - 12.73701045 * sy + 3727.44522025);
}

class HmiKtp400MobileTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
    }

    void OnReady() override {
        emu_.Get<Tsc2017HostState>().SetPen(false, 0x800u, 0x800u);
    }

    void OnPenDown(int x, int y) override { Apply(true, x, y); }
    void OnPenMove(int x, int y) override { Apply(true, x, y); }
    void OnPenUp(int x, int y) override   { Apply(false, x, y); }
    void OnCaptureLost() override         { Apply(false, last_x_, last_y_); }

private:
    static constexpr uint32_t kWidth = 480u;
    static constexpr uint32_t kHeight = 272u;

    void Apply(bool down, int x, int y) {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= static_cast<int>(kWidth)) x = static_cast<int>(kWidth - 1u);
        if (y >= static_cast<int>(kHeight)) y = static_cast<int>(kHeight - 1u);
        last_x_ = x;
        last_y_ = y;
        uint16_t raw_x = 0;
        uint16_t raw_y = 0;
        ScreenToRaw(static_cast<double>(x), static_cast<double>(y), raw_x, raw_y);
        emu_.Get<Tsc2017HostState>().SetPen(down, raw_x, raw_y);
    }

    int last_x_ = 240;
    int last_y_ = 136;
};

}  // namespace

REGISTER_SERVICE_AS(HmiKtp400MobileTouchInput, TouchInput);

