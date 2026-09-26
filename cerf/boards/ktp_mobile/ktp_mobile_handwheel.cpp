#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../socs/imx6/imx6_i2c_bus.h"
#include "../../socs/imx6/imx6_i2c_device.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

constexpr uint32_t kI2c1Base = 0x021A0000u;
constexpr uint8_t kSlaveAddress = 0x2Fu;
constexpr uint8_t kVersionRequest = 0x55u;

/* hmi_ktp700f_hw_mobile_v16 and _v17 drive one exchange on I2C1 slave 0x2F: a three-byte
   write of 0x55 0x00 0x00 and a five-byte read, after which the guest's own I2C_HW_Protocol
   prints "Handwheel version: 0.0 (Command: 0)"; the field layout behind it is not established. */
constexpr std::array<uint8_t, 5> kVersionAbsentStub = {0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
constexpr std::size_t kCommandFrameBytes = 3u;

class KtpMobileHandwheel final : public Imx6I2cDevice {
public:
    using Imx6I2cDevice::Imx6I2cDevice;

    bool ShouldRegister() override {
        const auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoard() == Board::HmiKtp700FHwMobile;
    }

    void OnReady() override { emu_.Get<Imx6I2cBus>().Register(this, kI2c1Base, kSlaveAddress); }

    void StartTransfer(bool read) override {
        byte_index_ = 0;
        if (read) BuildResponse();
    }

    void WriteByte(uint8_t value) override {
        if (byte_index_ >= kCommandFrameBytes)
            emu_.Get<Fatal>().Die("KTP hand wheel command frame longer than %u bytes",
                                  static_cast<unsigned>(kCommandFrameBytes));
        if (byte_index_ == 0)
            command_ = value;
        else if (value != 0u)
            emu_.Get<Fatal>().Die("KTP hand wheel command 0x%02X argument byte 0x%02X is not modelled",
                                  command_, value);
        ++byte_index_;
    }

    uint8_t ReadByte() override {
        if (byte_index_ >= response_.size())
            emu_.Get<Fatal>().Die("KTP hand wheel read past the %u-byte response",
                                  static_cast<unsigned>(response_.size()));
        return response_[byte_index_++];
    }

    void SaveState(StateWriter& writer) override {
        writer.Write(command_);
        writer.Write<uint32_t>(static_cast<uint32_t>(byte_index_));
        writer.WriteBytes(response_.data(), response_.size());
    }

    void RestoreState(StateReader& reader) override {
        reader.Read(command_);
        uint32_t index = 0;
        reader.Read(index);
        byte_index_ = index;
        reader.ReadBytes(response_.data(), response_.size());
    }

private:
    void BuildResponse() {
        if (command_ != kVersionRequest)
            emu_.Get<Fatal>().Die("KTP hand wheel command 0x%02X is not modelled", command_);
        response_ = kVersionAbsentStub;
    }

    uint8_t command_ = 0u;
    std::size_t byte_index_ = 0;
    std::array<uint8_t, 5> response_ = kVersionAbsentStub;
};

}

REGISTER_SERVICE(KtpMobileHandwheel);
