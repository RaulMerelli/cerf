#include "../armv7a_coproc_emitter_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../socs/omap3530/omap3530_id.h"
#include "../../socs/imx51/imx51_id.h"

namespace {

class CortexA8CoprocEmitter : public Armv7aCoprocEmitterBase {
public:
    using Armv7aCoprocEmitterBase::Armv7aCoprocEmitterBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const std::string_view soc = bd->GetSocId();
        return soc == SocId::Omap3530 || soc == SocId::Imx51;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CortexA8CoprocEmitter, CoprocEmitter);
