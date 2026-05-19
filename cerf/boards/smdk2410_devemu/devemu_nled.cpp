#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../board_detector.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace {

constexpr uint32_t kBase = 0x500FFFD0u;
constexpr uint32_t kSize = 0x00000006u;
constexpr uint32_t kNumLeds      = 2u;
constexpr uint32_t kBytesPerLed  = 3u;

constexpr uint32_t kFieldOnOffBlink = 0u;
constexpr uint32_t kFieldOnTime     = 1u;
constexpr uint32_t kFieldOffTime    = 2u;

class DevEmuNotificationLed : public Peripheral {
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

    uint8_t ReadByte (uint32_t addr) override;
    void    WriteByte(uint32_t addr, uint8_t value) override;

private:
    static const char* FieldName(uint32_t led_off);

    mutable std::mutex                            state_mutex_;
    std::array<uint8_t, kNumLeds * kBytesPerLed>  regs_{};
};

const char* DevEmuNotificationLed::FieldName(uint32_t led_off) {
    switch (led_off) {
        case kFieldOnOffBlink: return "OnOffBlink";
        case kFieldOnTime:     return "OnTime";
        case kFieldOffTime:    return "OffTime";
        default:               return "?";
    }
}

uint8_t DevEmuNotificationLed::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    uint8_t value = 0;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (off >= regs_.size()) {
            HaltUnsupportedAccess("ReadByte", addr, 0);  /* noreturn */
        }
        value = regs_[off];
    }
    const uint32_t led     = off / kBytesPerLed;
    const uint32_t led_off = off % kBytesPerLed;
    LOG(Periph, "[NLED] read8 led=%u %s -> 0x%02X\n",
        led, FieldName(led_off), value);
    return value;
}

void DevEmuNotificationLed::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off     = addr - kBase;
    const uint32_t led     = off / kBytesPerLed;
    const uint32_t led_off = off % kBytesPerLed;
    LOG(Periph, "[NLED] write8 led=%u %s = 0x%02X\n",
        led, FieldName(led_off), value);
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (off >= regs_.size()) {
        HaltUnsupportedAccess("WriteByte", addr, value);  /* noreturn */
    }
    regs_[off] = value;
}

}  /* namespace */

REGISTER_SERVICE(DevEmuNotificationLed);
