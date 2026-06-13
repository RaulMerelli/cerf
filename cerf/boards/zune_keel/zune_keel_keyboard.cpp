#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../host/keyboard_input.h"
#include "../../host/keyboard_map.h"
#include "../../socs/imx31/imx31_kpp.h"

#include <cstdint>

namespace {

class ZuneKeelKeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::ZuneKeel;
    }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        const uint8_t col = static_cast<uint8_t>(code >> 8);
        const uint8_t row = static_cast<uint8_t>(code & 0xFFu);
        emu_.Get<Imx31Kpp>().SetMatrixKey(col, row, !key_up);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(ZuneKeelKeyboardInput, KeyboardInput);
