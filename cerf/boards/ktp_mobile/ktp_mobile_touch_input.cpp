#include "../board_context.h"
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

/* The panel geometry and the calibration suffix its ROM uses; the five
   calibration points themselves come from that ROM at boot. */
struct PanelTouch {
    uint32_t width;
    uint32_t height;
    const char* size_suffix;
};

constexpr PanelTouch kKtp400Touch = {480u, 272u, "4in"};
constexpr PanelTouch kKtp700Touch = {800u, 480u, "7_9in"};
constexpr PanelTouch kTp1000fTouch = {800u, 480u, "10in"};

class KtpMobileTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }

    void OnReady() override {
        const PanelTouch& p = Panel();
        map_ = emu_.Get<KtpMobileTouchCalibration>().Read(p.size_suffix, p.width, p.height);
        emu_.Get<Tsc2017HostState>().SetPen(false, 0x800u, 0x800u);
    }

    void OnPenDown(int x, int y) override { Apply(true, x, y); }
    void OnPenMove(int x, int y) override { Apply(true, x, y); }
    void OnPenUp(int x, int y) override { Apply(false, x, y); }
    void OnCaptureLost() override { Apply(false, last_x_, last_y_); }

private:
    const PanelTouch& Panel() const {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return kKtp400Touch;
        switch (bd->GetBoard()) {
        case Board::HmiKtp700Mobile:
        case Board::HmiKtp700FMobile:
        case Board::HmiKtp900Mobile:
        case Board::HmiKtp900FMobile: return kKtp700Touch;
        case Board::HmiTp1000fMobile: return kTp1000fTouch;
        case Board::HmiKtp700FHwMobile:
        case Board::HmiKtp700FArcticMobile: return kKtp700Touch;
        default: return kKtp400Touch;
        }
    }

    void Apply(bool down, int x, int y) {
        const PanelTouch& p = Panel();
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= static_cast<int>(p.width)) x = static_cast<int>(p.width - 1u);
        if (y >= static_cast<int>(p.height)) y = static_cast<int>(p.height - 1u);
        last_x_ = x;
        last_y_ = y;
        if (!map_.valid) return;
        const double sx = static_cast<double>(x);
        const double sy = static_cast<double>(y);
        emu_.Get<Tsc2017HostState>().SetPen(down, Clamp12(map_.ax * sx + map_.bx * sy + map_.cx),
                                            Clamp12(map_.ay * sx + map_.by * sy + map_.cy));
    }

    KtpMobileTouchMap map_{};
    int last_x_ = 240;
    int last_y_ = 136;
};

} // namespace

REGISTER_SERVICE_AS(KtpMobileTouchInput, TouchInput);
