#include "../../socs/imx6/imx6_i2c_bus.h"
#include "../../socs/imx6/imx6_i2c_device.h"
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>
#include <ctime>

namespace {

constexpr uint8_t kRegSeconds = 0x00u;
constexpr uint8_t kRegYear = 0x06u;
constexpr uint8_t kRegFlag = 0x0Eu;    /* VLF (voltage-drop) bit 0x02. */
constexpr uint8_t kRegControl = 0x0Fu; /* STOP bit 0x02. */
constexpr uint8_t kFlagVlf = 0x02u;
constexpr uint8_t kControlStop = 0x02u;

constexpr uint8_t Bcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10u) << 4) | (value % 10u));
}

bool BcdToBin(uint8_t value, int mask, int maximum, int& result) {
    const int v = value & mask;
    const int high = (v >> 4) & 0x0F;
    const int low = v & 0x0F;
    if (high > 9 || low > 9) return false;
    result = high * 10 + low;
    return result <= maximum;
}

std::tm LocalTime(std::time_t value) {
    std::tm result{};
#if defined(_WIN32)
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

/* hmi_ktp400_mobile_v13 RTC8571.dll set_time @0xEF2E2830 / get_time @0xEF2E2B58: regs 0..6 =
   sec/min/hour BCD, weekday one-hot bit (1<<wday), day/month BCD,
   2-digit year BCD (get_time pivot: <80 -> 20xx, else 19xx); reg 0x0E
   bit1 = VLF, reg 0x0F bit1 = clock STOP. */
class Rtc8571 final : public Imx6I2cDevice {
public:
    using Imx6I2cDevice::Imx6I2cDevice;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && BoardContext::IsKtpMobile(board->GetBoard());
    }

    void OnReady() override {
        MaterializeClockRegisters();
        emu_.Get<Imx6I2cBus>().Register(this);
    }

    uint32_t I2cControllerBase() const override { return 0x021A8000u; }
    uint8_t SlaveAddress() const override { return 0x32u; }

    void StartTransfer(bool read) override {
        expecting_pointer_ = !read;
        if (read && !Stopped()) MaterializeClockRegisters();
    }

    void WriteByte(uint8_t value) override {
        if (expecting_pointer_) {
            pointer_ = value;
            expecting_pointer_ = false;
            return;
        }
        const uint8_t index = pointer_ & 0x0Fu;
        const bool was_stopped = Stopped();
        registers_[index] = value;
        if (index == kRegControl && was_stopped && !Stopped()) CommitClockRegisters();
        pointer_ = static_cast<uint8_t>(pointer_ + 1u);
    }

    uint8_t ReadByte() override { return registers_[pointer_++ & 0x0Fu]; }

    void SaveState(StateWriter& writer) override {
        for (uint8_t value : registers_)
            writer.Write(value);
        writer.Write(pointer_);
        writer.Write(epoch_delta_seconds_);
    }

    void RestoreState(StateReader& reader) override {
        for (uint8_t& value : registers_)
            reader.Read(value);
        reader.Read(pointer_);
        reader.Read(epoch_delta_seconds_);
        expecting_pointer_ = true;
    }

private:
    bool Stopped() const { return (registers_[kRegControl] & kControlStop) != 0; }

    void MaterializeClockRegisters() {
        const std::time_t now = std::time(nullptr) + static_cast<std::time_t>(epoch_delta_seconds_);
        const std::tm local = LocalTime(now);
        int year2 = (local.tm_year + 1900) % 100;
        registers_[0] = Bcd(static_cast<uint8_t>(local.tm_sec));
        registers_[1] = Bcd(static_cast<uint8_t>(local.tm_min));
        registers_[2] = Bcd(static_cast<uint8_t>(local.tm_hour));
        registers_[3] = static_cast<uint8_t>(1u << local.tm_wday);
        registers_[4] = Bcd(static_cast<uint8_t>(local.tm_mday));
        registers_[5] = Bcd(static_cast<uint8_t>(local.tm_mon + 1));
        registers_[kRegYear] = Bcd(static_cast<uint8_t>(year2));
        registers_[kRegFlag] &= static_cast<uint8_t>(~kFlagVlf);
    }

    void CommitClockRegisters() {
        int second = 0, minute = 0, hour = 0, day = 0, month = 0, year2 = 0;
        if (!BcdToBin(registers_[0], 0x7F, 59, second) || !BcdToBin(registers_[1], 0x7F, 59, minute) ||
            !BcdToBin(registers_[2], 0x3F, 23, hour) || !BcdToBin(registers_[4], 0x3F, 31, day) || day < 1 ||
            !BcdToBin(registers_[5], 0x1F, 12, month) || month < 1 ||
            !BcdToBin(registers_[kRegYear], 0xFFu, 99, year2)) {
            return;
        }
        std::tm target{};
        target.tm_sec = second;
        target.tm_min = minute;
        target.tm_hour = hour;
        target.tm_mday = day;
        target.tm_mon = month - 1;
        target.tm_year = (year2 < 80 ? 2000 + year2 : 1900 + year2) - 1900;
        target.tm_isdst = -1;
        const std::time_t timestamp = std::mktime(&target);
        if (timestamp != static_cast<std::time_t>(-1))
            epoch_delta_seconds_ = static_cast<int64_t>(timestamp - std::time(nullptr));
    }

    uint8_t registers_[16] = {0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00};
    uint8_t pointer_ = 0;
    bool expecting_pointer_ = true;
    int64_t epoch_delta_seconds_ = 0;
};

} // namespace

REGISTER_SERVICE(Rtc8571);
