#include "../../jit/mips/mips_cp0_emitter.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

namespace {

/* NEC VR4121 (VR4120 core): no SoC-specific CP0 moves; uses the base MipsCp0Emitter. */
class Vr4121Cp0Emitter : public MipsCp0Emitter {
public:
    using MipsCp0Emitter::MipsCp0Emitter;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4121;
    }
};

}  // namespace

REGISTER_SERVICE_AS(Vr4121Cp0Emitter, MipsCp0Emitter);
