#include "simpad_sl4_keypad.h"

#include "../../host/keyboard_input.h"
#include "../board_detector.h"
#include "../../core/cerf_emulator.h"

#include <array>

bool SimpadSl4Keypad::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::SimpadSl4;
}

REGISTER_SERVICE(SimpadSl4Keypad);

namespace {

/* Win32 VK -> SIMpad keypad button (0..5), -1 = unmapped. The guest's
   keyucb1x00 KeyMap turns each button into its VKEY (5=Enter, 4=Esc, 2=PgUp,
   3=PgDn, 0=F1, 1=Alt), so the host keys are picked to land on the natural
   button: arrows drive the 4-way nav, Enter/Esc alias the OK/Back buttons. */
constexpr int VkToButton(uint8_t vk) {
    switch (vk) {
        case 0x26: case 0x21: return 2;  /* Up    / PageUp -> UP   */
        case 0x28: case 0x22: return 3;  /* Down  / PageDn -> DOWN */
        case 0x25: case 0x1B: return 4;  /* Left  / Esc    -> LEFT */
        case 0x27: case 0x0D: return 5;  /* Right / Enter  -> RIGHT (OK) */
        case 0x70:            return 0;  /* F1 -> PROG1 */
        case 0x71:            return 1;  /* F2 -> PROG2 */
        default:              return -1;
    }
}

class SimpadSl4KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

    void OnHostKey(uint8_t vk, bool key_up) override {
        if (VkToButton(vk) < 0) return;
        down_[vk] = !key_up;
        uint8_t mask = 0;
        for (int v = 0; v < 256; ++v) {
            const int b = VkToButton(static_cast<uint8_t>(v));
            if (b >= 0 && down_[v]) mask |= static_cast<uint8_t>(1u << b);
        }
        emu_.Get<SimpadSl4Keypad>().SetPressedMask(mask);
    }

private:
    std::array<bool, 256> down_{};
};

}  /* namespace */

REGISTER_SERVICE_AS(SimpadSl4KeyboardInput, KeyboardInput);
