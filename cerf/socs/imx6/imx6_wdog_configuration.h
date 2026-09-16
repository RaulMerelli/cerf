#pragma once

#include "../../core/service.h"

class Imx6WdogConfiguration : public Service {
public:
    using Service::Service;

    virtual bool PowerDownCounterEnabledAfterBoot() const = 0;
};
