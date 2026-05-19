#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../board_detector.h"

#include <cstdint>
#include <mutex>

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
        return emu_.Get<BoardDetector>().GetBoard() == Board::Smdk2410DevEmu;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t  ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;

private:
    mutable std::mutex state_mutex_;
    uint8_t            is_on_battery_  = 0;   /* on AC */
    uint8_t            charge_percent_ = 0;   /* emulator's "0=full" */
    uint16_t           temperature_    = 0;   /* raw, not interpreted */
};

uint8_t DevEmuBattery::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    uint8_t value = 0;
    const char* name = "?";
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (off) {
            case kRegIsOnBattery:
                value = is_on_battery_;
                name  = "IsOnBattery";
                break;
            case kRegChargePercent:
                value = charge_percent_;
                name  = "ChargePercent";
                break;
            default:
                HaltUnsupportedAccess("ReadByte", addr, 0);  /* noreturn */
        }
    }
    LOG(Periph, "[Battery] read8 %s -> 0x%02X\n", name, value);
    return value;
}

uint16_t DevEmuBattery::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    uint16_t value = 0;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (off) {
            case kRegTemperature:
                value = temperature_;
                break;
            default:
                HaltUnsupportedAccess("ReadHalf", addr, 0);  /* noreturn */
        }
    }
    LOG(Periph, "[Battery] read16 Temperature -> 0x%04X\n", value);
    return value;
}

}  /* namespace */

REGISTER_SERVICE(DevEmuBattery);
