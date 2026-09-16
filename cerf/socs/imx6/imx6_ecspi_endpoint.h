#pragma once

#include "../../core/service.h"

#include <cstddef>
#include <cstdint>

class Imx6EcspiEndpoint : public Service {
public:
    using Service::Service;
    ~Imx6EcspiEndpoint() override = default;

    virtual uint32_t EcspiBase() const = 0;
    virtual void StageDmaTransmit(uint32_t buffer_pa, uint32_t bytes) = 0;
    virtual void StageDmaReceive(uint32_t buffer_pa, uint32_t bytes) = 0;
    virtual bool HasStagedTransmit() const = 0;
    virtual bool Exchange(uint32_t conreg, uint32_t configreg) = 0;
};
