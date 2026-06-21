#pragma once

#include "../../core/service.h"

#include <cstdint>

/* Board-side consumer of iPAQ EGPIO latch changes (the option-pack
   power enable lives there). Optional - TryGet'd by the latch. */
class IpaqGen1EgpioSink : public Service {
public:
    using Service::Service;
    ~IpaqGen1EgpioSink() override = default;

    virtual void OnEgpioChanged(uint32_t latched) = 0;
};
