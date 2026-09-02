#include "xscale_coproc_emitter_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../jit/arm/arm_cpu.h"
#include "../../jit/arm/arm_emit_services.h"
#include "../../jit/arm/arm_interrupt_channel.h"
#include "../../jit/arm/place_fns.h"
#include "../../socs/iop13xx/iop13xx_cp6.h"

namespace {

class Iop13xxXscaleCoprocEmitter final : public XscaleCoprocEmitterBase {
public:
    using XscaleCoprocEmitterBase::XscaleCoprocEmitterBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::IOP13xx;
    }

    uint8_t* EmitRegisterTransfer(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) override {
        if (d->cp_num == 15 && !d->l && d->crn == 7 && d->cp_opc == 1) {
            const bool invalidate_mva = d->crm == 7 && d->cp == 1;
            const bool clean = d->crm == 11 && (d->cp == 1 || d->cp == 2);
            const bool clean_invalidate = d->crm == 15 && d->cp == 2;
            if (invalidate_mva || clean || clean_invalidate) return cursor;
        }
        if (d->cp_num == 6) {
            return static_cast<Iop13xxCp6&>(emu_.Get<IrqController>()).EmitRegisterTransfer(cursor, d, ctx);
        }
        return XscaleCoprocEmitterBase::EmitRegisterTransfer(cursor, d, ctx);
    }

protected:
    uint8_t* EmitPwrmodeWrite(uint8_t* cursor, uint8_t, DecodedInsn* d, BlockContext* ctx) override {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitUnhandledCoprocessor(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) override {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
};

} // namespace

REGISTER_SERVICE_AS(Iop13xxXscaleCoprocEmitter, CoprocEmitter);
