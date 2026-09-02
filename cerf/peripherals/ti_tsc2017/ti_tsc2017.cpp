#include "../../socs/imx6/imx6_i2c_bus.h"
#include "../../socs/imx6/imx6_i2c_device.h"
#include "tsc2017_host_state.h"
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"

namespace {

/* TI TSC2017 touchscreen controller, I2C slave 0x49 on i.MX6 I2C3. A command
   byte selects a conversion function; the result is clocked back over one or
   two read bytes. Host pointer state â†’ raw ADC comes from Tsc2017HostState. */
class TiTsc2017 : public Imx6I2cDevice {
public:
    using Imx6I2cDevice::Imx6I2cDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }
    void OnReady() override { emu_.Get<Imx6I2cBus>().Register(this); }

    uint32_t I2cControllerBase() const override { return 0x021A8000u; } /* I2C3 */
    uint8_t SlaveAddress() const override { return 0x49u; }

    void StartTransfer(bool read) override {
        expecting_command_ = !read;
        pending_completion_ = false;
        if (read) read_index_ = 0;
    }

    void WriteByte(uint8_t value) override {
        if (!expecting_command_)
            emu_.Get<Fatal>().Die("TSC2017 unexpected additional write byte 0x%02X", value);
        Command(value);
        pending_completion_ = true;
        expecting_command_ = false;
    }

    uint8_t ReadByte() override {
        const bool eight_bit = (command_ & 0x02u) != 0;
        const uint16_t sample = Clamp12(last_value_);
        const uint8_t index = read_index_++;

        uint8_t out = 0;
        if (eight_bit) {
            out = index == 0 ? static_cast<uint8_t>(sample >> 4) : 0x00u;
        } else {
            out = index == 0 ? static_cast<uint8_t>(sample >> 4) : static_cast<uint8_t>((sample & 0x000Fu) << 4);
        }
        const uint8_t function = static_cast<uint8_t>((command_ >> 4) & 0x0Fu);
        if (function == 0x0Fu && ((eight_bit && index == 0u) || (!eight_bit && index == 1u))) {
            AdvanceFrame(function);
        }
        return out;
    }

    bool TakePendingCompletion() override {
        const bool p = pending_completion_;
        pending_completion_ = false;
        return p;
    }

    void SaveState(StateWriter& w) override {
        w.Write(command_);
        w.Write(read_index_);
        w.Write(last_value_);
        w.Write(setup_);
    }
    void RestoreState(StateReader& r) override {
        r.Read(command_);
        r.Read(read_index_);
        r.Read(last_value_);
        r.Read(setup_);
        frame_active_ = false;
        frame_z2_count_ = 0;
        pending_completion_ = false;
        expecting_command_ = false;
    }

private:
    static uint16_t Clamp12(uint16_t v) { return static_cast<uint16_t>(v & 0x0FFFu); }

    Tsc2017HostState::Sample FrameSample() { return frame_active_ ? frame_ : emu_.Get<Tsc2017HostState>().Get(); }

    void BeginFrameIfNeeded(uint8_t function) {
        /* SBAS472 Table 3 (p. 25): C3-C0 = 1010 is "Activate Y+, X- drivers",
           the driver-activation command that precedes a measurement group.
           Latch one host sample there and hold it across the group, so the
           X (1100) and Y (1101) conversions inside one group cannot report
           two different contact points. */
        if (function == 0x0Au && !frame_active_) {
            frame_ = emu_.Get<Tsc2017HostState>().Get();
            frame_active_ = true;
            frame_z2_count_ = 0;
        }
    }

    void AdvanceFrame(uint8_t function) {
        if (!frame_active_ || function != 0x0Fu) return;
        if (++frame_z2_count_ >= 3u) {
            frame_active_ = false;
            frame_z2_count_ = 0;
        }
    }

    /* Converter function select, SBAS472 Table 3 (p. 25): C3-C0 selects
       0000 TEMP0, 0010 AUX, 0100 TEMP1, 1000/1001/1010 driver activation,
       1100 X position, 1101 Y position, 1110 Z1 position, 1111 Z2 position. */
    uint16_t SampleForFunction(uint8_t function) {
        const auto touch = FrameSample();
        switch (function & 0x0Fu) {
        case 0x0u: return 0x300u; /* TEMP0 */
        case 0x2u: return 0x800u; /* AUX mid-scale */
        case 0x4u: return 0x300u; /* TEMP1 */
        case 0x8u:                /* driver-activation commands, not ADC */
        case 0x9u:
        case 0xAu: return 0x000u;
        case 0xCu: return touch.x;
        case 0xDu: return touch.y;
        case 0xEu: return touch.z1;
        case 0xFu: return touch.z2;
        default: emu_.Get<Fatal>().Die("TSC2017 reserved converter function 0x%X", function);
        }
    }

    void Command(uint8_t command) {
        command_ = command;
        read_index_ = 0;
        const uint8_t function = static_cast<uint8_t>(command >> 4);
        BeginFrameIfNeeded(function);
        if (function == 0x0Bu) { /* setup command */
            setup_ = static_cast<uint8_t>(command & 0x0Fu);
            if (setup_ & 0x04u) { /* software reset self-clears */
                setup_ = 0;
                last_value_ = 0;
            }
            return;
        }
        last_value_ = Clamp12(SampleForFunction(function));
    }

    uint8_t command_ = 0, read_index_ = 0, setup_ = 0;
    uint16_t last_value_ = 0;
    Tsc2017HostState::Sample frame_{};
    uint8_t frame_z2_count_ = 0;
    bool frame_active_ = false;
    bool expecting_command_ = false;
    bool pending_completion_ = false;
};

} // namespace

REGISTER_SERVICE(TiTsc2017);
