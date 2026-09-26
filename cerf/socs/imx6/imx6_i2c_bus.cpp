#include "imx6_i2c_bus.h"

#include "imx6_i2c_device.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"

REGISTER_SERVICE(Imx6I2cBus);

void Imx6I2cBus::Register(Imx6I2cDevice* device, uint32_t controller_base, uint8_t slave_addr) {
    if (Find(controller_base, slave_addr))
        emu_.Get<Fatal>().Die("i.MX6 I2C controller 0x%08X already has a slave at 0x%02X", controller_base,
                              slave_addr);
    devices_.push_back({device, controller_base, slave_addr});
}

Imx6I2cDevice* Imx6I2cBus::Find(uint32_t controller_base, uint8_t slave_addr) const {
    for (const Attachment& a : devices_) {
        if (a.controller_base == controller_base && a.slave_addr == slave_addr) return a.device;
    }
    return nullptr;
}

void Imx6I2cBus::SaveDevices(uint32_t controller_base, StateWriter& w) const {
    for (const Attachment& a : devices_) {
        if (a.controller_base == controller_base) a.device->SaveState(w);
    }
}

void Imx6I2cBus::RestoreDevices(uint32_t controller_base, StateReader& r) const {
    for (const Attachment& a : devices_) {
        if (a.controller_base == controller_base) a.device->RestoreState(r);
    }
}
