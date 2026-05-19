#include "../../jit/coproc_emitter.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>

#include "../../core/cerf_emulator.h"
#include "../../jit/arm_jit.h"
#include "../../jit/cpu_state.h"
#include "../../jit/place_fns.h"
#include "../../jit/x86_emit.h"
#include "../../boards/board_detector.h"

namespace {

class Sa1110CoprocEmitter : public CoprocEmitter {
public:
    using CoprocEmitter::CoprocEmitter;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetBoard() == Board::Ipaq3650;
    }

    /* SA-110 Data Sheet §3.3: cp15 is the only coprocessor on
       StrongARM; any other cp_num and all LDC/STC/CDP raise UND. */
    uint8_t* EmitRegisterTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) override {
        if (d->cp_num != 15) {
            return EmitRaiseUndAndReturn(cursor, d, ctx);
        }
        /* SA-1110 c15 (Dev Man §5.2 "Test, clock, idle"): writes have
           no software-visible state; reads reserved. Intercept BEFORE
           the shared cp15 emit, which treats c15 as ARM920T's CAC and
           UND-faults on Rd bits 31:14 (kernel writes 0x80020000). */
        if (d->crn == 15) {
            /* MCR p15, 0, Rd, c15, c2, 2 — SA-1110 "Wait for Interrupt"
               (Dev Man §5.3.4). OEMIdle uses this to halt CPU until next
               IRQ. Without this, the kernel polls cp15 + LCD-mmio 137K
               times/sec instead of sleeping. */
            if (!d->l && d->crm == 2 && d->cp == 2 && d->cp_opc == 0) {
                using namespace x86;
                EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
                EmitCall(cursor, reinterpret_cast<void*>(&ArmJit::WfiHelper));
                return cursor;
            }
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
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataOperation(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) override {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Sa1110CoprocEmitter, CoprocEmitter);
