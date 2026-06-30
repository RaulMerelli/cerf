#include "simpad_sl4_keypad.h"

#include "../../host/keyboard_input.h"
#include "../../host/keyboard_map.h"
#include "../../host/keyboard_router.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"

#include <array>

bool SimpadSl4Keypad::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SimpadSl4;
}

REGISTER_SERVICE(SimpadSl4Keypad);

namespace {

class SimpadSl4KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    void OnHostKey(uint8_t vk, bool key_up) override {
        auto& map = emu_.Get<KeyboardMap>();
        uint32_t code;
        if (!map.BaseDeviceCode(vk, code)) return;
        down_[vk] = !key_up;
        uint8_t mask = 0;
        for (int v = 0; v < 256; ++v) {
            uint32_t btn;
            if (down_[v] && map.BaseDeviceCode(static_cast<uint8_t>(v), btn))
                mask |= static_cast<uint8_t>(1u << btn);
        }
        emu_.Get<SimpadSl4Keypad>().SetPressedMask(mask);
    }

private:
    std::array<bool, 256> down_{};
};

}  /* namespace */

REGISTER_SERVICE(SimpadSl4KeyboardInput);
