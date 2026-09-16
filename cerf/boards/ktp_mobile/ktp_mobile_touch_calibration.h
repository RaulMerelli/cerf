#pragma once

#include "../../core/service.h"

#include <cstdint>

struct KtpMobileTouchMap {
    bool valid = false;
    double ax = 0.0, bx = 0.0, cx = 0.0;
    double ay = 0.0, by = 0.0, cy = 0.0;
};

class KtpMobileTouchCalibration : public Service {
public:
    using Service::Service;

    KtpMobileTouchMap Read(const char* size_suffix, uint32_t width, uint32_t height);
};
