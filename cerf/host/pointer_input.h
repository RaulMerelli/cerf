#pragma once

#include "../core/service.h"

#include <cstdint>

/* Absolute mouse (hover, buttons, wheel) for guest additions, vs the touch
   pen. sx,sy are guest-surface pixels (same space as TouchInput); the
   concrete normalizes. Registered only under --guest-additions, where the
   board TouchInput concrete is gated off — exactly one input path wins. */
/* button_mask bits, shared by HostCanvas and the channel adapter. */
constexpr uint32_t kPointerLeft   = 0x1u;
constexpr uint32_t kPointerRight  = 0x2u;
constexpr uint32_t kPointerMiddle = 0x4u;

class PointerInput : public Service {
public:
    using Service::Service;
    ~PointerInput() override = default;

    virtual void OnMove (int sx, int sy, uint32_t button_mask) = 0;
    virtual void OnWheel(int sx, int sy, int delta)            = 0;
    virtual void OnCaptureLost()                               = 0;
};
