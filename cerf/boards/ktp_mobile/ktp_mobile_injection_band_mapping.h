#pragma once

#include "../../core/service.h"

#include <cstdint>

class KtpMobileInjectionBandMapping final : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void EnsureBandMappedIntoGuestTables();

private:
    bool installed_ = false;
};
