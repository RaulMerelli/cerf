#pragma once

#include "../../core/service.h"

#include <cstddef>
#include <cstdint>

/* Optional board-level endpoint for an i.MX6 eCSPI controller.  SDMA hands
   complete host buffers to this seam so a synchronous emulator does not have
   to pretend that more than the documented 64 words fit in the hardware FIFO.
   The controller remains the owner of MMIO registers and status bits. */
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
