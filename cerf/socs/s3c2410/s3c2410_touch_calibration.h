#pragma once

#include "../../core/service.h"

#include <cstdint>

/* The two S3C2410 ADC/touch facts that are panel/BSP-specific (the silicon
   itself is identical across boards): the host-pixel -> 10-bit-sample mapping
   and whether the X/Y data registers are wired swapped. */
class S3C2410TouchCalibration : public Service {
public:
    using Service::Service;

    /* Map host-window client-area pixels to the panel's 10-bit ADC X/Y
       samples. screen_w/screen_h are the live guest-surface dimensions. */
    virtual void MapHostToSample(int host_x, int host_y,
                                 double screen_w, double screen_h,
                                 uint16_t& sample_x, uint16_t& sample_y) const = 0;

    /* True when the BSP wires the axes swapped: ADCDAT0/XPDATA carries the Y
       sample and ADCDAT1/YPDATA carries X. */
    virtual bool AxisSwap() const = 0;
};
