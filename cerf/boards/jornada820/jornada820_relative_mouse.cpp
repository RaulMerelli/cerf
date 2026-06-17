#include "../../host/relative_mouse_input.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "jornada820_companion_asic.h"

namespace {

class Jornada820RelativeMouse : public RelativeMouseInput {
public:
    using RelativeMouseInput::RelativeMouseInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }

    void OnRelativeMove(int dx, int dy, uint32_t button_mask) override {
        emu_.Get<Jornada820CompanionAsic>().QueuePs2Motion(dx, dy, button_mask);
    }

    std::wstring SourceName() const override { return L"Stock PS/2 mouse"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada820RelativeMouse, RelativeMouseInput);
