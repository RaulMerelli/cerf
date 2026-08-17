#include "../../jit/arm/coproc_emitter.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>

#include "../../core/cerf_emulator.h"
#include "../../jit/arm/cpu_state.h"
#include "../../jit/arm/place_fns.h"
#include "../../jit/x86_emit.h"
#include "../../boards/board_context.h"

namespace {

class Arm1136CoprocEmitter : public CoprocEmitter {
public:
    using CoprocEmitter::CoprocEmitter;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX31;
    }

    uint8_t* EmitRegisterTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) override {
        if (d->cp_num == 10 || d->cp_num == 11) {
            return EmitVfpRegisterTransfer(cursor, d, ctx);
        }
        /* CP14 is the ARM1136 debug coprocessor (DDI 0211 Chapter 13:
           "CP14 registers reset" p. 13-43, "CP14 debug instructions"
           p. 13-44). */
        if (d->cp_num == 14) {
            return EmitCoprocUnimplementedFatal(cursor, d, ctx);
        }
        if (d->cp_num != 15) {
            return EmitRaiseUndAndReturn(cursor, d, ctx);
        }

        /* CRn=15 implementation-defined; iMX31 boot stub writes
           0x40000015 which would fatal in shared dispatch.  Read
           returns 0; write is no-op. */
        if (d->crn == 15) {
            if (d->l) {
                using namespace x86;
                const int32_t rd_disp = static_cast<int32_t>(
                    offsetof(ArmCpuState, gprs) + d->rd * 4u);
                EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp, 0u);
            }
            return cursor;
        }

        return EmitCp15RegisterTransfer(cursor, d, ctx);
    }

    uint8_t* EmitDataTransfer(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) override {
        if (d->cp_num == 10 || d->cp_num == 11) {
            return EmitVfpDataTransfer(cursor, d, ctx);
        }
        /* DDI 0211I Table 13-28 (p. 13-44): "STC p14, c5, <addressing mode>"
           and "LDC p14, c5, <addressing mode>" are CP14 debug instructions,
           accessing the rDTR / wDTR. p. 13-45: "If the processor tries to
           execute a CP14 debug instruction that either is not in Table 13-28
           on page 13-44, or is targeted to a reserved register ... the
           Undefined instruction exception is taken." */
        if (d->cp_num == 14 && d->crd == 5u) {
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

REGISTER_SERVICE_AS(Arm1136CoprocEmitter, CoprocEmitter);
