#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_input.h"
#include "../../host/keyboard_map.h"
#include "../../host/keyboard_router.h"
#include "sharp_mobilon_hc4100_key_matrix.h"
#include "../board_context.h"

#include <cstdint>
#include <string>

namespace {

class SharpMobilonHc4100KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    std::wstring SourceName() const override { return L"HC-4100 keyboard"; }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        emu_.Get<SharpMobilonHc4100KeyMatrix>().SetKey(
            static_cast<uint8_t>(code >> 4), static_cast<uint8_t>(code & 0xF), !key_up);
    }
};

}  /* namespace */

REGISTER_SERVICE(SharpMobilonHc4100KeyboardInput);
