#include "jornada820_keyboard.h"

#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_input.h"
#include "../../host/keyboard_map.h"
#include "../../host/keyboard_router.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include "../../socs/sa11xx/sa11xx_ssp_device.h"
#include "../board_context.h"

#include <deque>

namespace {

/* SSP keyboard controller slave (keybddr sub_12C21B8 init / sub_12C239C reads).
   Init command { 0x1B ESC, cmd, params } one byte/frame, reply { 0x80, ack, data }
   clocked by dummy frames: cmd 0xA0 -> ack 0xA1, cmd 0xA9 = 16-byte config. Past
   init, an idle dummy read returns the next queued scancode (one per frame). */
class Jornada820KbdSspSlave : public Sa11xxSspDevice {
public:
    using Sa11xxSspDevice::Sa11xxSspDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }

    uint16_t Exchange(uint16_t tx_frame) override {
        const uint8_t b = static_cast<uint8_t>(tx_frame & 0xFFu);

        if (b == 0x1Bu) {              /* ESC always begins a fresh command */
            reply_.clear();
            cmd_ = -1;
            params_left_ = 0;
            in_cmd_ = true;
            return 0;
        }
        if (!reply_.empty()) {         /* driver is clocking out the reply */
            const uint8_t r = reply_.front();
            reply_.pop_front();
            return r;
        }
        if (in_cmd_) {
            if (cmd_ < 0) {            /* the command byte after ESC */
                cmd_ = b;
                params_left_ = ParamCount(b);
                if (params_left_ == 0) FinishCommand();
            } else if (params_left_ > 0 && --params_left_ == 0) {
                FinishCommand();
            }
            return 0;
        }
        return emu_.Get<Jornada820Keyboard>().NextScancode();  /* post-init keystroke */
    }

private:
    static int ParamCount(uint8_t cmd) {
        switch (cmd) {
            case 0xA0: return 1;       /* { ESC, A0, 7B } */
            case 0xA9: return 16;      /* { ESC, A9, 16 bytes } */
            default:   return 0;
        }
    }

    void FinishCommand() {
        if (cmd_ == 0xA0) {
            reply_.assign({0x80u, 0xA1u, 0x00u});  /* marker, ack, discard */
        }
        in_cmd_ = false;
        cmd_ = -1;
    }

    std::deque<uint8_t> reply_;
    int  cmd_         = -1;
    int  params_left_ = 0;
    bool in_cmd_      = false;
};
REGISTER_SERVICE_AS(Jornada820KbdSspSlave, Sa11xxSspDevice);

class Jornada820KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }
    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }
    void OnHostKey(uint8_t vk, bool key_up) override {
        emu_.Get<Jornada820Keyboard>().OnHostKey(vk, key_up);
    }
};
REGISTER_SERVICE(Jornada820KeyboardInput);

}  /* namespace */

REGISTER_SERVICE(Jornada820Keyboard);

bool Jornada820Keyboard::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::Jornada820;
}

void Jornada820Keyboard::OnHostKey(uint8_t vk, bool key_up) {
    uint32_t code;
    if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
    const uint8_t sc = static_cast<uint8_t>(code);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_.push_back(key_up ? static_cast<uint8_t>(sc | 0x80u) : sc);
    }
    PulseKbdIrqLine();
}

uint16_t Jornada820Keyboard::NextScancode() {
    uint8_t sc;
    bool more;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (pending_.empty()) return 0;
        sc = pending_.front();
        pending_.pop_front();
        more = !pending_.empty();
    }
    /* The IST reads exactly one scancode per interrupt (keybddr sub_12C2604),
       so each queued scancode needs its own GPIO0 edge. */
    if (more) PulseKbdIrqLine();
    return sc;
}

void Jornada820Keyboard::PulseKbdIrqLine() {
    /* GPIO0 idles low (the init poll keybddr sub_12C1980 reads replies while
       low); a key drives a low->high->low transient so the high->low falling
       edge (GFER bit0; GRER bit0 is clear) latches GEDR bit0 -> INTC source 0,
       leaving GPIO0 back at its idle-low level. */
    auto& gpio = emu_.Get<Sa11xxGpio>();
    gpio.DriveInputPin(0, true);
    gpio.DriveInputPin(0, false);
}
