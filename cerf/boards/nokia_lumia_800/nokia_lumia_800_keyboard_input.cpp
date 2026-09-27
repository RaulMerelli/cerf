#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_input.h"
#include "../../host/keyboard_map.h"
#include "../../host/keyboard_router.h"
#include "../../peripherals/qualcomm_pm8058/pm8058_keypad.h"
#include "../board_context.h"
#include "nokia_lumia_800_id.h"

#include <cstdint>

namespace {

constexpr uint32_t kRowShift = 8u;
constexpr uint32_t kColMask  = 0xFFu;

class NokiaLumia800KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardId() == BoardId::NokiaLumia800;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code = 0;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        emu_.Get<Pm8058Keypad>().SetKeyPressed(code >> kRowShift,
                                               code & kColMask, !key_up);
    }
};

}

REGISTER_SERVICE(NokiaLumia800KeyboardInput);
