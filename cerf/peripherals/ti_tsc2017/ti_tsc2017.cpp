#include "../../socs/imx6/imx6_i2c_device.h"
#include "ti_tsc2017_wiring.h"
#include "tsc2017_host_state.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"

/* hmi_ktp400_mobile_v13 touch.dll carries the PDB name "touch_tsc2017.pdb" at offset
   0x21CE, so the panel drives a Texas Instruments TSC2017. */

namespace {

class TiTsc2017 : public Imx6I2cDevice {
public:
    using Imx6I2cDevice::Imx6I2cDevice;

    bool ShouldRegister() override { return emu_.TryGet<TiTsc2017Wiring>() != nullptr; }
    void OnReady() override { emu_.Get<TiTsc2017Wiring>().Attach(this); }

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
        /* SBAS472 section 8.5.2: a conversion result is one byte in 8-bit mode and two in
           12-bit mode; the device has nothing further to hand out. */
        const uint8_t result_bytes = eight_bit ? 1u : 2u;
        /* SBAS472 page 27: "If the master somehow acknowledges the second data byte,
           invalid data are returned (FFh). This condition applies to both 12- and
           8-bit modes." */
        if (index >= result_bytes) return 0xFFu;

        const uint8_t out = index == 0 ? static_cast<uint8_t>(sample >> 4)
                                       : static_cast<uint8_t>((sample & 0x000Fu) << 4);
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
        w.Write("command", command_);
        w.Write("read_index", read_index_);
        w.Write("last_value", last_value_);
        w.Write("setup", setup_);
        w.Write("frame_down", static_cast<uint8_t>(frame_.down));
        w.Write("frame_x", frame_.x);
        w.Write("frame_y", frame_.y);
        w.Write("frame_z1", frame_.z1);
        w.Write("frame_z2", frame_.z2);
        w.Write("frame_z2_count", frame_z2_count_);
        w.Write("frame_active", static_cast<uint8_t>(frame_active_));
        w.Write("expecting_command", static_cast<uint8_t>(expecting_command_));
        w.Write("pending_completion", static_cast<uint8_t>(pending_completion_));
    }
    void RestoreState(StateReader& r) override {
        r.Read("command", command_);
        r.Read("read_index", read_index_);
        r.Read("last_value", last_value_);
        r.Read("setup", setup_);
        uint8_t frame_down = 0, frame_active = 0, expecting_command = 0, pending_completion = 0;
        r.Read("frame_down", frame_down);
        r.Read("frame_x", frame_.x);
        r.Read("frame_y", frame_.y);
        r.Read("frame_z1", frame_.z1);
        r.Read("frame_z2", frame_.z2);
        r.Read("frame_z2_count", frame_z2_count_);
        r.Read("frame_active", frame_active);
        r.Read("expecting_command", expecting_command);
        r.Read("pending_completion", pending_completion);
        if (frame_down > 1u || frame_active > 1u || expecting_command > 1u || pending_completion > 1u)
            r.Reject("TSC2017: saved flag is not a boolean");
        if (frame_z2_count_ >= 3u) r.Reject("TSC2017: frame Z2 count %u past the third read", frame_z2_count_);
        frame_.down = frame_down != 0u;
        frame_active_ = frame_active != 0u;
        expecting_command_ = expecting_command != 0u;
        pending_completion_ = pending_completion != 0u;
    }

private:
    static uint16_t Clamp12(uint16_t v) { return static_cast<uint16_t>(v & 0x0FFFu); }

    Tsc2017HostState::Sample FrameSample() { return frame_active_ ? frame_ : emu_.Get<Tsc2017HostState>().Get(); }

    void BeginFrameIfNeeded(uint8_t function) {
        /* TSC2017 datasheet SBAS472 Table 3: command 1010 activates Y+ and X- drivers. */
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

    /* SBAS472: TEMP0 and TEMP1 are the two internal diode voltages and AUX the auxiliary input
       pin; their codes follow die temperature and a board rail, neither of which CERF carries. */
    static constexpr uint16_t kTemp0AbsentStub = 0x300u;
    static constexpr uint16_t kAuxAbsentStub = 0x800u;
    static constexpr uint16_t kTemp1AbsentStub = 0x300u;

    /* Converter function select, SBAS472 Table 3 (p. 25): C3-C0 selects
       0000 TEMP0, 0010 AUX, 0100 TEMP1, 1000/1001/1010 driver activation,
       1100 X position, 1101 Y position, 1110 Z1 position, 1111 Z2 position. */
    uint16_t SampleForFunction(uint8_t function) {
        const auto touch = FrameSample();
        switch (function & 0x0Fu) {
        case 0x0u: return kTemp0AbsentStub;
        case 0x2u: return kAuxAbsentStub;
        case 0x4u: return kTemp1AbsentStub;
        case 0x8u:
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
        if (function == 0x0Bu) {
            /* SBAS472 Table 4: D3 is "Reserved; must write '0'". */
            if (command & 0x08u)
                emu_.Get<Fatal>().Die("TSC2017 setup command 0x%02X writes reserved D3", command);
            setup_ = static_cast<uint8_t>(command & 0x0Fu);
            if (setup_ & 0x04u) {
                setup_ = 0;
                last_value_ = 0;
            }
            return;
        }
        /* SBAS472 Table 2: D3-D2 select 00 power down between cycles with PENIRQ enabled,
           01 and 11 A/D converter on with PENIRQ disabled, and 10 A/D converter off. CERF
           drives PENIRQ from the host touch state and converts on every command. */
        if ((command & 0x0Cu) != 0u)
            emu_.Get<Fatal>().Die("TSC2017 command 0x%02X selects power-down mode %u", command,
                                  static_cast<unsigned>((command >> 2) & 0x03u));
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

}

REGISTER_SERVICE(TiTsc2017);
