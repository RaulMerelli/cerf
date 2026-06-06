#define NOMINMAX

#include "jornada720_touch.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_canvas.h"
#include "../../host/touch_input.h"
#include "../../socs/sa1110/sa1110_gpio.h"
#include "../board_detector.h"

namespace {

/* Virtual panel raw-ADC characteristic (10-bit, linear across the surface).
   touch.dll's affine matrix calibrates it, so the align-screen applet adapts
   to whatever extents are produced here. */
constexpr uint16_t kAdcXMin = 130, kAdcXMax = 900;
constexpr uint16_t kAdcYMin = 120, kAdcYMax = 900;
constexpr auto     kSamplePeriod = std::chrono::milliseconds(20);

uint16_t MapAxis(int v, int v_max, uint16_t lo, uint16_t hi) {
    if (v_max <= 0) return lo;
    if (v <= 0)     return lo;
    if (v >= v_max) return hi;
    return static_cast<uint16_t>(lo + (uint32_t)v * (hi - lo) / (uint32_t)v_max);
}

class Jornada720TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }
    void OnPenDown(int x, int y) override { emu_.Get<Jornada720Touch>().PenDown(x, y); }
    void OnPenMove(int x, int y) override { emu_.Get<Jornada720Touch>().PenMove(x, y); }
    void OnPenUp  (int /*x*/, int /*y*/) override { emu_.Get<Jornada720Touch>().PenUp(); }
    void OnCaptureLost() override { emu_.Get<Jornada720Touch>().PenUp(); }
};

}  /* namespace */

REGISTER_SERVICE(Jornada720Touch);
REGISTER_SERVICE_AS(Jornada720TouchInput, TouchInput);

bool Jornada720Touch::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::Jornada720;
}

void Jornada720Touch::OnReady() {
    /* GPLR bit 9 idles high = pen up; CERF input pins default low, so publish
       the pen-up level before the driver's first GPLR read. */
    DrivePenLine(/*pen_down=*/false);
    sampler_ = std::thread([this] { SamplerLoop(); });
}

Jornada720Touch::~Jornada720Touch() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
    if (sampler_.joinable()) sampler_.join();
}

void Jornada720Touch::DrivePenLine(bool pen_down) {
    /* GPIO9: low = pen down (touch.dll collects), high = pen up. */
    emu_.Get<Sa1110Gpio>().DriveInputPin(9, /*level=*/!pen_down);
}

void Jornada720Touch::PulsePenLine() {
    /* HP doc §4.4: a new sample is signalled by pulsing GPIO9 (high then low).
       The rising edge latches the driver's armed GRER bit 9 -> IRQ; by the time
       its IST reads GPLR the line is low again -> it collects a fresh sample. */
    auto& gpio = emu_.Get<Sa1110Gpio>();
    gpio.DriveInputPin(9, true);
    gpio.DriveInputPin(9, false);
}

void Jornada720Touch::MapHostToAdc(int hx, int hy, uint16_t* ax, uint16_t* ay) const {
    auto&     hc = emu_.Get<HostCanvas>();
    const int w  = (int)hc.GuestSurfaceWidth();
    const int h  = (int)hc.GuestSurfaceHeight();
    *ax = MapAxis(hx, w - 1, kAdcXMin, kAdcXMax);
    *ay = MapAxis(hy, h - 1, kAdcYMin, kAdcYMax);
}

void Jornada720Touch::CurrentAdc(uint16_t* adc_x, uint16_t* adc_y) const {
    std::lock_guard<std::mutex> lk(mtx_);
    *adc_x = adc_x_;
    *adc_y = adc_y_;
}

void Jornada720Touch::PenDown(int x, int y) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        synthetic_pulses_ = 0;             /* host pen overrides a bezel tap */
        last_x_ = x;
        last_y_ = y;
        pen_down_ = true;
        MapHostToAdc(x, y, &adc_x_, &adc_y_);
    }
    DrivePenLine(/*pen_down=*/true);   /* falling edge -> first sample IRQ */
    cv_.notify_all();
}

void Jornada720Touch::TapRawAdc(uint16_t adc_x, uint16_t adc_y) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        adc_x_ = adc_x;
        adc_y_ = adc_y;
        /* touch.dll latches the zone from pen-down samples and fires on the
           pen-up report (sub_FB1314) — hold a few samples, then release. */
        synthetic_pulses_ = 4;
        pen_down_ = true;
    }
    DrivePenLine(/*pen_down=*/true);
    cv_.notify_all();
}

void Jornada720Touch::PenMove(int x, int y) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!pen_down_) return;
    last_x_ = x;
    last_y_ = y;
}

void Jornada720Touch::PenUp() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!pen_down_) return;
        synthetic_pulses_ = 0;
        pen_down_ = false;
    }
    DrivePenLine(/*pen_down=*/false);  /* rising edge -> pen-up IRQ */
    cv_.notify_all();
}

void Jornada720Touch::SamplerLoop() {
    std::unique_lock<std::mutex> lk(mtx_);
    while (!stop_) {
        if (pen_down_) {
            if (synthetic_pulses_ > 0) {
                if (--synthetic_pulses_ == 0) {
                    pen_down_ = false;
                    lk.unlock();
                    DrivePenLine(/*pen_down=*/false);  /* end the bezel tap */
                    lk.lock();
                    continue;
                }
            } else {
                MapHostToAdc(last_x_, last_y_, &adc_x_, &adc_y_);
            }
            lk.unlock();
            PulsePenLine();
            lk.lock();
            cv_.wait_for(lk, kSamplePeriod, [&] { return stop_ || !pen_down_; });
        } else {
            cv_.wait(lk, [&] { return stop_ || pen_down_; });
        }
    }
}
