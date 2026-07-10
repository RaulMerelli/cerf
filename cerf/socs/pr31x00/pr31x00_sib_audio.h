#pragma once

#include "../../core/service.h"

#include <cstdint>

class Pr31x00SibAudioSink : public Service {
public:
    using Service::Service;

    virtual void StartSoundTx(uint32_t src_pa, uint32_t bytes, uint32_t rate_hz) = 0;
    virtual void StopSoundTx() = 0;
};
