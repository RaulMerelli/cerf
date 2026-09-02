#pragma once

#include <cstdint>
#include <vector>

#include "../../core/service.h"
#include "../../state/state_stream.h"

class Imx6I2cDevice;

/* Registry of Imx6I2cDevice slaves, keyed by (controller base, slave address).
   Devices self-register from OnReady; each Imx6I2c controller resolves the
   addressed slave through Find and forwards its byte exchange to it. */
class Imx6I2cBus : public Service {
public:
    using Service::Service;

    void Register(Imx6I2cDevice* device);
    Imx6I2cDevice* Find(uint32_t controller_base, uint8_t slave_addr) const;

    /* Hibernation forwarding: save/restore every device on one controller, in
       registration order (stable within a build). */
    void SaveDevices(uint32_t controller_base, StateWriter& w) const;
    void RestoreDevices(uint32_t controller_base, StateReader& r) const;

private:
    std::vector<Imx6I2cDevice*> devices_;
};
