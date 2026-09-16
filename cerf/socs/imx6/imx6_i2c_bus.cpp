#include "imx6_i2c_bus.h"

#include "imx6_i2c_device.h"
#include "../../core/cerf_emulator.h"

REGISTER_SERVICE(Imx6I2cBus);

void Imx6I2cBus::Register(Imx6I2cDevice* device) {
    if (device) devices_.push_back(device);
}

Imx6I2cDevice* Imx6I2cBus::Find(uint32_t controller_base, uint8_t slave_addr) const {
    for (Imx6I2cDevice* d : devices_) {
        if (d->I2cControllerBase() == controller_base && d->SlaveAddress() == slave_addr) return d;
    }
    return nullptr;
}

void Imx6I2cBus::SaveDevices(uint32_t controller_base, StateWriter& w) const {
    for (Imx6I2cDevice* d : devices_) {
        if (d->I2cControllerBase() == controller_base) d->SaveState(w);
    }
}

void Imx6I2cBus::RestoreDevices(uint32_t controller_base, StateReader& r) const {
    for (Imx6I2cDevice* d : devices_) {
        if (d->I2cControllerBase() == controller_base) d->RestoreState(r);
    }
}
