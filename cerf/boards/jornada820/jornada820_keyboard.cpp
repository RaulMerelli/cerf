#include "jornada820_keyboard.h"

#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_input.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include "../../socs/sa11xx/sa11xx_ssp_device.h"
#include "../board_detector.h"

#include <deque>

namespace {

/* Win32 VK -> HP Jornada 820 matrix scancode (0 = none): inverse of keybddr.dll's
   scancode->VK table @ 0x12C16F8. A frame's bit7 is make/break (set = key-up,
   keybddr sub_12C239C). Generic VK_SHIFT/CONTROL/MENU fold to the left-modifier
   scancodes (LSHIFT 14, LCONTROL 104, LMENU 23). */
constexpr uint8_t kVkToScancode[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x06, 0x00, 0x00, 0x00, 0x4E, 0x00, 0x00,   // 0x00
    0x0E, 0x68, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00,   // 0x10
    0x28, 0x00, 0x00, 0x00, 0x00, 0x4F, 0x47, 0x50, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00,   // 0x20
    0x4A, 0x02, 0x0A, 0x12, 0x1A, 0x22, 0x2A, 0x32, 0x3A, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0x30
    0x00, 0x04, 0x25, 0x15, 0x14, 0x13, 0x1C, 0x24, 0x2C, 0x3B, 0x34, 0x3C, 0x44, 0x35, 0x2D, 0x43,   // 0x40
    0x4B, 0x03, 0x1B, 0x0C, 0x23, 0x33, 0x1D, 0x0B, 0x0D, 0x2B, 0x05, 0x18, 0x00, 0x00, 0x00, 0x00,   // 0x50
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0x60
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0x70
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0x80
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0x90
    0x0E, 0x56, 0x68, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0xA0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D, 0x53, 0x36, 0x52, 0x3E, 0x46,   // 0xB0
    0x1F, 0x01, 0x09, 0x11, 0x19, 0x21, 0x29, 0x31, 0x39, 0x41, 0x49, 0x51, 0x00, 0x00, 0x00, 0x00,   // 0xC0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x55, 0x4D, 0x45, 0x70,   // 0xD0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0xE0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0xF0
};

/* SSP keyboard controller slave (keybddr sub_12C21B8 init / sub_12C239C reads).
   Init command { 0x1B ESC, cmd, params } one byte/frame, reply { 0x80, ack, data }
   clocked by dummy frames: cmd 0xA0 -> ack 0xA1, cmd 0xA9 = 16-byte config. Past
   init, an idle dummy read returns the next queued scancode (one per frame). */
class Jornada820KbdSspSlave : public Sa11xxSspDevice {
public:
    using Sa11xxSspDevice::Sa11xxSspDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
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
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }
    void OnHostKey(uint8_t vk, bool key_up) override {
        emu_.Get<Jornada820Keyboard>().OnHostKey(vk, key_up);
    }
};
REGISTER_SERVICE_AS(Jornada820KeyboardInput, KeyboardInput);

}  /* namespace */

REGISTER_SERVICE(Jornada820Keyboard);

bool Jornada820Keyboard::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::Jornada820;
}

void Jornada820Keyboard::OnHostKey(uint8_t vk, bool key_up) {
    const uint8_t sc = kVkToScancode[vk];
    if (sc == 0) return;
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
