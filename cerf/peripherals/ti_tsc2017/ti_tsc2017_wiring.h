#pragma once

#include "../../core/service.h"

class Imx6I2cDevice;

class TiTsc2017Wiring : public Service {
public:
    using Service::Service;

    virtual void Attach(Imx6I2cDevice* device) = 0;
};
