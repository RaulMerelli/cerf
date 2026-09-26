#pragma once

#include "../../core/service.h"

#include <cstdint>

class SdCard;

class SdCardConfiguration : public Service {
public:
    using Service::Service;

    virtual uint64_t MediaSizeBytes() const = 0;
    virtual void Configure(SdCard& card) = 0;
};
