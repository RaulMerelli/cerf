#pragma once

#include "../../core/service.h"

#include <cstddef>
#include <cstdint>

/* The framebuffer + scanout geometry the Rage XL CRTC currently presents, decoded
   from the Mach64 CRTC registers and read by the FrameRenderer. Implemented by the
   AtiRageXl device; resolved via Get<RageXlDisplay>(). */
class RageXlDisplay : public Service {
public:
    using Service::Service;

    struct Frame {
        bool           on      = false;
        uint32_t       width   = 0;    /* visible pixels per line */
        uint32_t       height  = 0;    /* visible lines */
        uint32_t       bpp     = 0;    /* 8 / 15 / 16 / 24 / 32 */
        uint32_t       stride  = 0;    /* bytes per scanline */
        uint32_t       start   = 0;    /* byte offset of the visible base in the framebuffer */
        const uint8_t* fb      = nullptr;
        size_t         fb_size = 0;
    };

    virtual Frame CurrentFrame() const = 0;
};
