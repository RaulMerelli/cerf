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
        return bd && bd->GetBoard() == Board::ZuneKeel;
    }

    uint8_t* EmitRegisterTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) override {
        if (d->cp_num == 10 || d->cp_num == 11) {
            return EmitVfpRegisterTransfer(cursor, d, ctx);
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
