#pragma once

#include "../core/service.h"

#include <cstdint>

/* Pre-boot guest-surface size for the host window and boot banner, so the
   open size doesn't flicker before the LCD reports. Combines configurable
   screen size with a board's fixed-LCD preferred size. Superseded by the
   real LCD-reported size once it arrives via OnLcdEnabled. */
class InitialWindowSize : public Service {
public:
    using Service::Service;

    struct Size { uint32_t width; uint32_t height; };
    Size Resolve() const;
};
