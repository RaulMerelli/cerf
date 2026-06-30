#include "jornada720_keyboard.h"

#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_input.h"
#include "../../host/keyboard_map.h"
#include "../../host/keyboard_router.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include "../board_context.h"

namespace {

class Jornada720KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }
    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }
    void OnHostKey(uint8_t vk, bool key_up) override {
        emu_.Get<Jornada720Keyboard>().OnHostKey(vk, key_up);
    }
};

}  /* namespace */

REGISTER_SERVICE(Jornada720Keyboard);
REGISTER_SERVICE(Jornada720KeyboardInput);

bool Jornada720Keyboard::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::Jornada720;
}

void Jornada720Keyboard::OnReady() {
    /* GPIO0 idles high; a key pulls it low (falling edge). Publish idle-high so
       the first key produces a clean falling edge (HP doc §4.3). */
    emu_.Get<Sa11xxGpio>().DriveInputPin(0, /*level=*/true);
}

void Jornada720Keyboard::OnHostKey(uint8_t vk, bool key_up) {
    uint32_t code;
    if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
    const uint8_t sc = static_cast<uint8_t>(code);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_.push_back(key_up ? static_cast<uint8_t>(sc | 0x80) : sc);
    }
    PulseKbdIrqLine();
}

void Jornada720Keyboard::DrainScancodes(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lk(mtx_);
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

void Jornada720Keyboard::PulseKbdIrqLine() {
    /* Drive the high->low edge that latches Sa11xxGpio GEDR bit0 -> INTC source
       0 (HP doc §4.3 falling-edge), then return to idle high for the next key. */
    auto& gpio = emu_.Get<Sa11xxGpio>();
    gpio.DriveInputPin(0, false);
    gpio.DriveInputPin(0, true);
}
