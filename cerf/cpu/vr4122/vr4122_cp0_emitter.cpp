#include "../../jit/mips/mips_cp0_emitter.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

namespace {

class Vr4122Cp0Emitter : public MipsCp0Emitter {
public:
    using MipsCp0Emitter::MipsCp0Emitter;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4122;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4122Cp0Emitter, MipsCp0Emitter);
