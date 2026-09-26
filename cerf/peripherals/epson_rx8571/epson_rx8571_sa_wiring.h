#pragma once

#include "../../core/service.h"

class Imx6I2cDevice;

class EpsonRx8571SaWiring : public Service {
public:
    using Service::Service;

    virtual void Attach(Imx6I2cDevice* device) = 0;
};
