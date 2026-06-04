#pragma once

#include "../core/service.h"

#include <cstdint>

/* Codec behind a SoC AC'97 controller's AC-link. reg is the AC'97 codec
   register index (0x00..0x7E). The controller forwards codec-register access
   here when a codec is registered for the board, else uses its own shadow. */
class Ac97Codec : public Service {
public:
    using Service::Service;

    virtual uint16_t ReadReg(uint32_t reg) = 0;
    virtual void     WriteReg(uint32_t reg, uint16_t value) = 0;
};
