#pragma once

#include "../../core/service.h"

#include <cstdint>

/* The affine map from panel pixels to TSC2017 raw counts, built from the
   calibration the panel's own ROM carries. */
struct KtpMobileTouchMap {
    bool valid = false;
    double ax = 0.0, bx = 0.0, cx = 0.0;
    double ay = 0.0, by = 0.0, cy = 0.0;
};

/* Reads HKLM\HARDWARE\DEVICEMAP\TOUCH\CalibrationData out of the registry hive
   the ROM ships, then fits it against the crosshairs the touch calibration UI
   uses for a `width` x `height` panel.

   The V13 panels store one value named `CalibrationData`; the V17 ones name it
   per screen diagonal, so `size_suffix` ("4in", "7_9in", "10in") is tried
   first and the bare name second. */
class KtpMobileTouchCalibration : public Service {
public:
    using Service::Service;

    KtpMobileTouchMap Read(const char* size_suffix, uint32_t width, uint32_t height);
};
