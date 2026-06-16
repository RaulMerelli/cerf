#include "../../socs/s3c2410/s3c2410_touch_calibration.h"

#include "../../core/cerf_emulator.h"
#include "../board_detector.h"

namespace {

/* DeviceEmulator BSP IOADConverter::SetPenSample: X = 90 + host_x*(875/screen_x);
   Y = 920 - host_y*(870/screen_y), Y inverted (chip 0 = bottom). The BSP also
   wires the axes swapped (XPDATA carries Y). */
constexpr int    kSampleXOffset = 90;
constexpr double kSampleXSpan   = 875.0;
constexpr int    kSampleYOrigin = 920;
constexpr double kSampleYSpan   = 870.0;

class DevEmuTouchCalibration : public S3C2410TouchCalibration {
public:
    using S3C2410TouchCalibration::S3C2410TouchCalibration;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }

    void MapHostToSample(int host_x, int host_y,
                         double screen_w, double screen_h,
                         uint16_t& sample_x, uint16_t& sample_y) const override {
        sample_x = (uint16_t)(kSampleXOffset +
                              (int)((double)host_x * (kSampleXSpan / screen_w)));
        sample_y = (uint16_t)(kSampleYOrigin -
                              (int)((double)host_y * (kSampleYSpan / screen_h)));
    }

    bool AxisSwap() const override { return true; }
};

}  /* namespace */

REGISTER_SERVICE_AS(DevEmuTouchCalibration, S3C2410TouchCalibration);
