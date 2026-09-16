#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/imx6/imx6_i2c_bus.h"
#include "../../socs/imx6/imx6_i2c_device.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

constexpr uint32_t kI2c1Base = 0x021A0000u;
constexpr uint8_t kSlaveAddress = 0x2Fu;
constexpr uint8_t kVersionRequest = 0x55u;
constexpr uint8_t kDataRequest = 0x01u;

class KtpMobileHandwheel final : public Imx6I2cDevice {
public:
    using Imx6I2cDevice::Imx6I2cDevice;

    bool ShouldRegister() override {
        const auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoard() == Board::HmiKtp700FHwMobile;
    }

    void OnReady() override { emu_.Get<Imx6I2cBus>().Register(this); }

    uint32_t I2cControllerBase() const override { return kI2c1Base; }
    uint8_t SlaveAddress() const override { return kSlaveAddress; }

    void StartTransfer(bool read) override {
        byte_index_ = 0;
        if (read) BuildResponse();
    }

    void WriteByte(uint8_t value) override {
        if (byte_index_ == 0) command_ = value;
        ++byte_index_;
    }

    uint8_t ReadByte() override {
        if (byte_index_ >= response_.size()) return 0xFFu;
        return response_[byte_index_++];
    }

    void SaveState(StateWriter& writer) override { writer.Write(command_); }

    void RestoreState(StateReader& reader) override {
        reader.Read(command_);
        byte_index_ = 0;
        response_.fill(0xFFu);
    }

private:
    void BuildResponse() {
        response_.fill(0xFFu);
        switch (command_) {
        case kVersionRequest:
            response_ = {0x00u, 0x02u, 0x00u, 0x00u};
            break;
        case kDataRequest:
            response_ = {0x02u, 0x12u, 0x00u, 0x00u};
            break;
        default:
            break;
        }
    }

    uint8_t command_ = 0xFFu;
    std::size_t byte_index_ = 0;
    std::array<uint8_t, 4> response_ = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
};

} // namespace

REGISTER_SERVICE(KtpMobileHandwheel);
