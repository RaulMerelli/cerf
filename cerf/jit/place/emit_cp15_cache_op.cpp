#include <cstddef>
#include <cstdint>

#include "../../core/log.h"
#include "../../cpu/arm_processor_config.h"
#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* EmitCp15CacheOp(uint8_t*      cursor,
                         DecodedInsn*  d,
                         BlockContext* ctx) {
    using namespace x86;
    ArmJit* jit = ctx->jit;

    /* Loads from cp15 c7 are architecturally undefined (the register
       is write-only). */
    if (d->l) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t cache_line_size  = jit->ProcessorConfig()->CacheLineSize();
    const uint32_t pc_resume_plus_4 = d->guest_address + 4u;
    const uint32_t pc_resume_plus10 = d->guest_address + 0x10u;
    void* const flush_trampoline    = jit->FlushTranslationCacheTrampoline();

    const int32_t r15_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + ArmGpr::kR15 * 4u);
    const int32_t r1_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + ArmGpr::kR1 * 4u);
    const int32_t rd_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);

    switch (d->cp_opc) {
    case 0:
        switch (d->crm) {
        case 0:
            /* CP=4 wait-for-interrupt; CP=others architecturally
               undefined but treated as no-op emit per reference. */
            break;

        case 5:
            switch (d->cp) {
            case 0:
                /* Invalidate entire I-cache → full JIT flush. */
                EmitXorRegReg(cursor, kEcx, kEcx);
                EmitMovRegImm32(cursor, kEdx, 0xFFFFFFFFu);
                EmitMovBaseDisp32Imm32(cursor, kStateReg, r15_disp, pc_resume_plus_4);
                EmitJmp32(cursor, flush_trampoline);
                break;
            case 1:
                if (d->operand2 == 0xFFFFFFFFu) {
                    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, rd_disp);
                    EmitXorRegReg(cursor, kEdx, kEdx);
                    /* XCHG EDX, [ESI + gprs[R1]] — EDX ← R1, R1 ← 0. */
                    EmitXchgRegBaseDisp32(cursor, kEdx, kStateReg, r1_disp);
                    EmitMovBaseDisp32Imm32(cursor, kStateReg, r15_disp, pc_resume_plus10);
                    EmitJmp32(cursor, flush_trampoline);
                } else {
                    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, rd_disp);
                    EmitMovRegImm32(cursor, kEdx, cache_line_size);
                    EmitMovBaseDisp32Imm32(cursor, kStateReg, r15_disp, pc_resume_plus_4);
                    EmitJmp32(cursor, flush_trampoline);
                }
                break;
            case 2:
                /* Set/index invalidate not modeled — halt. */
                LOG(Caution,
                    "EmitCp15CacheOp: c7 CRm=5 CP=2 (set/index I-cache invalidate) not supported, pc=0x%08X\n",
                    d->guest_address);
                CerfFatalExit(2);
                break;
            case 4:  /* Flush Prefetch Buffer  — no-op. */
            case 6:  /* Flush Entire BTB        — no-op. */
            case 7:  /* Flush BTB entry         — no-op. */
                break;
            default:
                LOG(Caution,
                    "EmitCp15CacheOp: c7 CRm=5 CP=%u (unsupported I-cache op), pc=0x%08X\n",
                    d->cp, d->guest_address);
                CerfFatalExit(2);
                break;
            }
            break;

        case 6:
            /* D-cache invalidate. CERF has no D-cache, so all the
               documented sub-encodings are no-ops; unknown CPs halt. */
            switch (d->cp) {
            case 0:  /* Flush entire D-cache             */
            case 1:  /* Invalidate D-cache line by VA    */
            case 2:  /* Invalidate D-cache line by S/I   */
            case 4:
                break;
            default:
                LOG(Caution,
                    "EmitCp15CacheOp: c7 CRm=6 CP=%u (unsupported D-cache op), pc=0x%08X\n",
                    d->cp, d->guest_address);
                CerfFatalExit(2);
                break;
            }
            break;

        case 7:
            switch (d->cp) {
            case 0:
                /* Invalidate entire unified cache → full JIT flush. */
                EmitXorRegReg(cursor, kEcx, kEcx);
                EmitMovRegImm32(cursor, kEdx, 0xFFFFFFFFu);
                EmitMovBaseDisp32Imm32(cursor, kStateReg, r15_disp, pc_resume_plus_4);
                EmitJmp32(cursor, flush_trampoline);
                break;
            case 1:
                /* Invalidate unified-cache line (Rd = VA). */
                EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, rd_disp);
                EmitMovRegImm32(cursor, kEdx, cache_line_size);
                EmitMovBaseDisp32Imm32(cursor, kStateReg, r15_disp, pc_resume_plus_4);
                EmitJmp32(cursor, flush_trampoline);
                break;
            case 2:
            default:
                LOG(Caution,
                    "EmitCp15CacheOp: c7 CRm=7 CP=%u (unsupported unified cache op), pc=0x%08X\n",
                    d->cp, d->guest_address);
                CerfFatalExit(2);
                break;
            }
            break;

        case 8:   /* wait-for-interrupt   */
        case 10:  /* clean D-cache line   */
        case 11:  /* clean unified line   */
        case 13:  /* prefetch I-cache     */
        case 14:  /* clean+invalidate     */
        case 15:  /* clean+invalidate U   */
            /* No-ops on CERF — no D-cache, no prefetcher, no
               write buffer to drain. */
            break;

        default:
            /* Reference ASSERT(FALSE)s here; CERF surfaces the
               bad encoding as UND so guest code can see it. */
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            break;
        }
        break;

    case 1:
        /* "Flush" / "clean" D-cache entry by VA — documented
           sub-cases are no-ops on CERF; any other encoding UNDs. */
        switch (d->crm) {
        case 6:
        case 10:
            break;
        default:
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
            break;
        }
        break;

    case 4:
        /* Drain Write Buffer (CRm=10) — no-op (no write buffer on
           CERF). All other CRm values UND. */
        if (d->crm != 10) {
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        }
        break;

    case 6:
        /* Invalidate Branch Target Buffer (CRm=5) — no-op (no BTB
           on CERF). All other CRm values UND. */
        if (d->crm != 5) {
            cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        }
        break;

    default:
        cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
        break;
    }

    return cursor;
}
