#include "../../jit/arm/coproc_emitter.h"

#include "../../core/cerf_emulator.h"
#include "../../jit/arm/place_fns.h"
#include "../../boards/board_context.h"

namespace {

class Arm720TCoprocEmitter : public CoprocEmitter {
public:
    using CoprocEmitter::CoprocEmitter;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::Poseidon;
    }

    uint8_t* EmitRegisterTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) override {
        if (d->cp_num == 15) {
            return EmitCp15RegisterTransfer(cursor, d, ctx);
        }
        /* ARM720T TRM (DDI 0229C) §8.1.1 + Table 8-1: CP14 is the
           communications channel / debug controller coprocessor. */
        if (d->cp_num == 14) {
            return EmitCoprocUnimplementedFatal(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataTransfer(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) override {
        /* ARM720T TRM (DDI 0229C) §8.1.1 + Table 8-1: CP14 is the
           communications channel / debug controller coprocessor. */
        if (d->cp_num == 14) {
            return EmitCoprocDataTransferUnimplementedFatal(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataOperation(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) override {
        /* ARM720T TRM (DDI 0229C) §8.1.1 + Table 8-1: CP14 is the
           communications channel / debug controller coprocessor. */
        if (d->cp_num == 14) {
            return EmitCoprocDataOperationUnimplementedFatal(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Arm720TCoprocEmitter, CoprocEmitter);
