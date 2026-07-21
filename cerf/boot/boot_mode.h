#pragma once

#include "../core/service.h"

#include <cstdint>

/* Strategy for the guest CPU's cold-reset entry state. */
class BootMode : public Service {
public:
    using Service::Service;

    void OnReady() override;

    /* Initial PC at cold reset, PHYSICAL (MMU off at reset → PC consumed as PA). */
    virtual uint32_t ColdEntryPa() = 0;

    /* Initial SP at cold reset, PHYSICAL. */
    virtual uint32_t ColdStackPa() = 0;
};
