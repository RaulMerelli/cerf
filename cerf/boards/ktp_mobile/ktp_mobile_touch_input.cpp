#include "../board_context.h"
#include "ktp_mobile_board_profile.h"
#include "ktp_mobile_touch_calibration.h"
#include "../../core/cerf_emulator.h"
#include "../../host/touch_input.h"
#include "../../peripherals/ti_tsc2017/tsc2017_host_state.h"

#include <algorithm>

namespace {

uint16_t Clamp12(double v) {
    if (v < 0.0) v = 0.0;
    const uint32_t iv = static_cast<uint32_t>(v + 0.5);
    return static_cast<uint16_t>(std::min<uint32_t>(iv, 0x0FFFu));
}

class KtpMobileTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }

    void OnReady() override {
        profile_ = &KtpMobileBoardProfileFor(emu_.Get<BoardContext>().GetBoard());
        map_ = emu_.Get<KtpMobileTouchCalibration>().Read(
            profile_->touch_size_suffix, profile_->panel.width, profile_->panel.height);
        emu_.Get<Tsc2017HostState>().SetPen(false, 0x800u, 0x800u);
    }

    void OnPenDown(int x, int y) override { Apply(true, x, y); }
    void OnPenMove(int x, int y) override { Apply(true, x, y); }
    void OnPenUp(int x, int y) override { Apply(false, x, y); }
    void OnCaptureLost() override { Apply(false, last_x_, last_y_); }

private:
    void Apply(bool down, int x, int y) {
        const auto& panel = profile_->panel;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= static_cast<int>(panel.width)) x = static_cast<int>(panel.width - 1u);
        if (y >= static_cast<int>(panel.height)) y = static_cast<int>(panel.height - 1u);
        last_x_ = x;
        last_y_ = y;
        if (!map_.valid) return;
        const double sx = static_cast<double>(x);
        const double sy = static_cast<double>(y);
        emu_.Get<Tsc2017HostState>().SetPen(
            down, Clamp12(map_.ax * sx + map_.bx * sy + map_.cx),
            Clamp12(map_.ay * sx + map_.by * sy + map_.cy));
    }

    const KtpMobileBoardProfile* profile_ = nullptr;
    KtpMobileTouchMap map_{};
    int last_x_ = 240;
    int last_y_ = 136;
};

}

REGISTER_SERVICE_AS(KtpMobileTouchInput, TouchInput);
