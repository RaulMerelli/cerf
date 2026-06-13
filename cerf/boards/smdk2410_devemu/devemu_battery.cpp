#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/battery_widget.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../board_detector.h"

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x500FFFF0u;
constexpr uint32_t kSize = 0x00000004u;

constexpr uint32_t kRegIsOnBattery   = 0x00u;
constexpr uint32_t kRegChargePercent = 0x01u;
constexpr uint32_t kRegTemperature   = 0x02u;

class DevEmuBattery : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        battery_.BindEmulator(emu_);
        emu_.Get<HostWidgetRegistry>().Register(&battery_);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t  ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;

private:
    BatteryWidget battery_;
    uint16_t      temperature_ = 0;   /* raw, not interpreted */
};

uint8_t DevEmuBattery::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    uint8_t value = 0;
    const char* name = "?";
    switch (off) {
        case kRegIsOnBattery:
            value = battery_.IsOnBattery() ? 1u : 0u;   /* battif.c:33 — 0 = AC */
            name  = "IsOnBattery";
            break;
        case kRegChargePercent:
            /* battif.c:34 stores the inverse: 0 = full. */
            value = static_cast<uint8_t>(100 - battery_.FillPercent());
            name  = "ChargePercent";
            break;
        default:
            HaltUnsupportedAccess("ReadByte", addr, 0);  /* noreturn */
    }
    LOG(Periph, "[Battery] read8 %s -> 0x%02X\n", name, value);
    return value;
}

uint16_t DevEmuBattery::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (off != kRegTemperature) HaltUnsupportedAccess("ReadHalf", addr, 0);  /* noreturn */
    LOG(Periph, "[Battery] read16 Temperature -> 0x%04X\n", temperature_);
    return temperature_;
}

}  /* namespace */

REGISTER_SERVICE(DevEmuBattery);
