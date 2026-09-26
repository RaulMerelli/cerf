#pragma once

#include <cstdint>
#include <vector>

#include "../../core/service.h"
#include "../../state/state_stream.h"

class Imx6I2cDevice;

class Imx6I2cBus : public Service {
public:
    using Service::Service;

    void Register(Imx6I2cDevice* device, uint32_t controller_base, uint8_t slave_addr);
    Imx6I2cDevice* Find(uint32_t controller_base, uint8_t slave_addr) const;

    void SaveDevices(uint32_t controller_base, StateWriter& w) const;
    void RestoreDevices(uint32_t controller_base, StateReader& r) const;

private:
    struct Attachment {
        Imx6I2cDevice* device;
        uint32_t controller_base;
        uint8_t slave_addr;
    };

    std::vector<Attachment> devices_;
};
