#include "../../jit/arm/coproc_emitter.h"

#include "../../core/cerf_emulator.h"
#include "../../jit/arm/place_fns.h"
#include "../../boards/board_context.h"

namespace {

class CortexA8CoprocEmitter : public CoprocEmitter {
public:
    using CoprocEmitter::CoprocEmitter;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::OMAP3530 || soc == SocFamily::iMX51;
    }

    uint8_t* EmitRegisterTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) override {
        if (d->cp_num == 15) {
            return EmitCp15RegisterTransfer(cursor, d, ctx);
        }
        if (d->cp_num == 10 || d->cp_num == 11) {
            return EmitVfpRegisterTransfer(cursor, d, ctx);
        }
        /* CP14 is the Cortex-A8 debug control coprocessor (DDI 0344
           §1.3.2, Chapter 12 Debug). */
        if (d->cp_num == 14) {
            return EmitCoprocUnimplementedFatal(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataTransfer(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) override {
        if (d->cp_num == 10 || d->cp_num == 11) {
            return EmitVfpDataTransfer(cursor, d, ctx);
        }
        /* CP14 is the Cortex-A8 debug control coprocessor (DDI 0344
           §1.3.2, Chapter 12 Debug). */
        if (d->cp_num == 14) {
            return EmitCoprocDataTransferUnimplementedFatal(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataOperation(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) override {
        if (d->cp_num == 10 || d->cp_num == 11) {
            return EmitVfpDataOperation(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CortexA8CoprocEmitter, CoprocEmitter);
