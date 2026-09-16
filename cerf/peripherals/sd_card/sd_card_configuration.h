#pragma once

#include "../../core/service.h"

class SdCard;

class SdCardConfiguration : public Service {
public:
    using Service::Service;

    virtual void Configure(SdCard& card) = 0;
};
