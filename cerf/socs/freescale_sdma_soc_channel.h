#pragma once

#include "../core/service.h"

#include <cstdint>

class FreescaleSdmaSocChannel : public Service {
public:
    using Service::Service;

    virtual bool Handles(uint32_t channel, int event) const = 0;
    virtual void Complete(uint32_t channel, uint32_t mode, uint32_t buffer_pa) = 0;
};
