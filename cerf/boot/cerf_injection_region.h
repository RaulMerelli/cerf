#pragma once

#include "../core/service.h"

#include <cstdint>

class CerfInjectionRegion : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    uint32_t BandVaBase();
    uint32_t BandPaBase() const;
    uint32_t BandSize() const;
    bool BandRunsInPlace();

private:
    uint32_t va_base_ = 0;
    bool run_in_place_ = false;
};
