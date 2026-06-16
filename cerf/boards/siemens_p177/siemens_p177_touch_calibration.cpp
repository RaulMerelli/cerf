#include "../../socs/s3c2410/s3c2410_touch_calibration.h"

#include "../../core/cerf_emulator.h"
#include "../board_detector.h"

#include <cstdint>

namespace {

/* Siemens P177 resistive-touch calibration; only the host-pixel->sample map and
   axis wiring are panel-specific (the S3C2410 ADC silicon is shared). Grounded in
   touch.dll (p177\...\touch.cpp): sub_3422240 places the 5 GWES crosshairs at
   screen inset 2*(dim/20) (480x272 -> X taps at 48/432, Y at 26/246); sub_3422318
   reads ADCDAT0->X, ADCDAT1->Y (no swap -> AxisSwap=false). The raw sample tapped
   at each crosshair is default.hv CalibrationData, PointNumber order center/TL/BL/
   BR/TR. This map is the inverse of the driver's calibration so an injected sample
   round-trips to the tapped pixel; rawY falls as screenY rises (Y inverted). */
constexpr double kCalXLeftFrac  =  48.0 / 480.0;
constexpr double kCalXRightFrac = 432.0 / 480.0;
constexpr double kCalYTopFrac   =  26.0 / 272.0;
constexpr double kCalYBotFrac   = 246.0 / 272.0;
constexpr double kRawXLeft  = 130.0;    /* avg(126,134) */
constexpr double kRawXRight = 886.5;    /* avg(881,892) */
constexpr double kRawYTop   = 865.0;    /* avg(852,878) */
constexpr double kRawYBot   = 175.0;    /* avg(168,182) */

uint16_t Clamp10(double v) {
    if (v <    0.0) return 0;
    if (v > 1023.0) return 1023;
    return (uint16_t)(v + 0.5);
}

class SiemensP177TouchCalibration : public S3C2410TouchCalibration {
public:
    using S3C2410TouchCalibration::S3C2410TouchCalibration;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SiemensP177;
    }

    void MapHostToSample(int host_x, int host_y,
                         double screen_w, double screen_h,
                         uint16_t& sample_x, uint16_t& sample_y) const override {
        const double fx = (double)host_x / screen_w;
        const double fy = (double)host_y / screen_h;
        sample_x = Clamp10(kRawXLeft + (fx - kCalXLeftFrac) *
                           (kRawXRight - kRawXLeft) / (kCalXRightFrac - kCalXLeftFrac));
        sample_y = Clamp10(kRawYTop + (fy - kCalYTopFrac) *
                           (kRawYBot - kRawYTop) / (kCalYBotFrac - kCalYTopFrac));
    }

    /* sub_3422318 reads ADCDAT0 as X, ADCDAT1 as Y — not swapped. */
    bool AxisSwap() const override { return false; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensP177TouchCalibration, S3C2410TouchCalibration);
