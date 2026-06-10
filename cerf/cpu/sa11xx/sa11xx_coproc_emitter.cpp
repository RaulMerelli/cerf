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

class Sa11xxCoprocEmitter : public CoprocEmitter {
public:
    using CoprocEmitter::CoprocEmitter;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && (bd->GetSoc() == SocFamily::SA1110 || bd->GetSoc() == SocFamily::SA1100);
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
           no software-visible state; reads reserved. Kernel writes
           0x80020000 here — without the intercept the shared dispatch
           UNDs c15. */
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
        /* StrongARM c9 = Read-Buffer ops (Dev Man §5.2.10 table; reads
           RESERVED per Table 5-1). CERF's coherent memory holds no RB state
           and an allocate's access violation aborts at the later load (§6.4)
           — documented writes are no-ops. Linux 2.6.25 executes the
           user-access enable (MCR p15,0,R0,c9,c0,5) in early boot before
           its vectors page is mapped; UNDing it is fatal there. */
        if (d->crn == 9) {
            if (!d->l && d->cp_opc == 0 &&
                ((d->cp == 0 && d->crm == 0) ||
                 (d->cp == 1 && d->crm <= 3) ||
                 (d->cp == 2 && d->crm <= 11) ||
                 (d->cp == 4 && d->crm == 0) ||
                 (d->cp == 5 && d->crm == 0))) {
                return cursor;
            }
            return EmitRaiseUndAndReturn(cursor, d, ctx);
        }
        /* SA-1110 c14 = debug/breakpoint registers (Dev Man §5.2.13). CERF has
           no breakpoint unit, so writes are no-ops and reads return 0 — enough
           for the kernel sleep state-save (reads c14 at nk.exe 0x8008162C) to
           round-trip. The shared cp15 dispatch UNDs c14, which corrupts suspend. */
        if (d->crn == 14) {
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

REGISTER_SERVICE_AS(Sa11xxCoprocEmitter, CoprocEmitter);
