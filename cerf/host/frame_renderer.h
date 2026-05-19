#pragma once

#include "../core/service.h"

#include <cstdint>
#include <utility>

/* RenderInto MUST fill every byte of dib_bgra32[0..w*h-1] —
   HostWindow does not pre-clear between ticks and the buffer
   may still hold pixels from whichever renderer ran last tick. */

class FrameRenderer : public Service {
public:
    using Service::Service;
    ~FrameRenderer() override = default;

    virtual bool HasFrame() = 0;

    virtual void RenderInto(uint32_t* dib_bgra32,
                            uint32_t  width,
                            uint32_t  height) = 0;

    /* Translate guest-FB dimensions to the host-window dimensions
       the renderer expects to draw into. Identity by default;
       rotating renderers (e.g. Sa1110LcdRenderer) override to swap. */
    virtual std::pair<uint32_t, uint32_t>
    HostSizeFor(uint32_t fb_w, uint32_t fb_h) const {
        return {fb_w, fb_h};
    }
};
