#include <cstddef>
#include <cstdint>

#include "../../../cpu/arm_processor_config.h"
#include "../arm_emit_services.h"
#include "../arm_interrupt_channel.h"
#include "../arm_translation_cache.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

/* ARM ARM DDI 0406C.c Figure B3-32 (p. B3-1475) ARMv7; Table D12-8
   (pp. D12-2533/2534) ARMv6; Table D15-22 (pp. D15-2629/2630) ARMv4/v5;
   DDI 0100I Table B6-6 (pp. B6-21/B6-22) + B6.6.5 (p. B6-19) pre-v6.
   Unallocated ARMv7 encodings UNDEFINED (pp. B3-1475, Glossary-2737). */
uint8_t* EmitCp15CacheOp(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;
    const bool v7 = emit->ProcessorConfig()->HasCp15V7();
    const bool v6 = emit->ProcessorConfig()->HasCp15V6();

    if (d->cp_opc != 0) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    void* const icache_helper = reinterpret_cast<void*>(
        &ArmTranslationCache::InvalidateDirtyCodePagesHelper);
    const uint32_t icache_self = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(emit->TranslationCache()));

    if (d->l) {
        /* "MRC p15, 0, APSR_nzcv, c7, c10, 3 ; test and clean" (also c14,3);
           "A global cache dirty status bit is written to the Z flag"
           (p. D15-2630) - no data cache is modeled, so it reads clean:
           NZCV = 0b0100. */
        if ((d->crm == 10 || d->crm == 14) && d->cp == 3 && !v7) {
            if (d->rd != 15) {
                return EmitCoprocUnimplementedFatal(cursor, d, ctx);
            }
            EmitMovByteBaseDisp32Imm8(cursor, kStateReg, ArmNfDisp(), 0u);
            EmitMovByteBaseDisp32Imm8(cursor, kStateReg, ArmZfDisp(), 1u);
            EmitMovByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 0u);
            EmitMovByteBaseDisp32Imm8(cursor, kStateReg, ArmVfDisp(), 0u);
            return cursor;
        }
        /* CDSR, ARMv6 only: C bit[0] = 0 "Cache clean", bits[31:1] UNK
           (p. D12-2534). */
        if (d->crm == 10 && d->cp == 6 && v6 && !v7) {
            if (d->rd == 15) {
                return EmitCoprocUnimplementedFatal(cursor, d, ctx);
            }
            EmitMovBaseDisp32Imm32(cursor, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u),
                0u);
            return cursor;
        }
        /* PAR read (Figure B3-32 c4/0, R/W); ARMv6: D12.7.12 (p. D12-2538),
           ARMv6K + Security Extensions VA-to-PA support. */
        if ((v6 || v7) && (d->crm == 4 || d->crm == 8)) {
            return EmitCoprocUnimplementedFatal(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    switch (d->crm) {
        case 0:
            /* CP15WFI c7,0,c0,4 (Table D15-23, p. D15-2631; Table D12-11,
               p. D12-2538); UNPREDICTABLE on ARMv7 (p. B3-1499). */
            if (d->cp == 4 && !v7) {
                EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                        emit->InterruptChannel())));
                EmitCall(cursor,
                    reinterpret_cast<void*>(&ArmInterruptChannel::WfiHelper));
                return cursor;
            }
            /* SA-1110 Developer's Manual §5.2.8: six register 7 functions,
               none with CRm=c0; unlisted in ARM DDI 0100I Table B6-6
               (pp. B6-21/B6-22). simpad_sl4_hpc2000 nk.exe start 0x80081900
               executes it on resume, after the Flush I+D at 0x800818C0. */
            if (d->cp == 0 && !v6 && !v7) {
                EmitMovRegImm32(cursor, kEcx, icache_self);
                EmitCall(cursor, icache_helper);
                return cursor;
            }
            break;

        case 1:
            /* ARM DDI 0406C.c Figure B3-32 (p. B3-1475): ICIALLUIS is
               c7,c1,0 and BPIALLIS is c7,c1,6. CERF models one guest CPU,
               so ICIALLUIS has the same translation-cache effect as ICIALLU;
               no branch predictor state is modeled for BPIALLIS. */
            if (d->cp == 0u) {
                EmitMovRegImm32(cursor, kEcx, icache_self);
                EmitCall(cursor, icache_helper);
                return cursor;
            }
            if (d->cp == 6u) {
                return cursor;
            }
            break;

        case 5:
            /* ICIALLU c5/0, ICIMVAU c5/1 (Figure B3-32); the v4/v5/v6 rows
               add c5/2 invalidate-by-set/way (Tables D15-22, D12-8). */
            if (d->cp == 0 || d->cp == 1 || (d->cp == 2 && !v7)) {
                EmitMovRegImm32(cursor, kEcx, icache_self);
                EmitCall(cursor, icache_helper);
                return cursor;
            }
            /* CP15ISB c5/4 (Table D15-23; Table D12-11, p. D12-2538;
               Figure B3-32). */
            if (d->cp == 4) {
                return cursor;
            }
            /* BPIALL c5/6, BPIMVA c5/7 (Figure B3-32; Tables D15-22, D12-8). */
            if (d->cp == 6 || d->cp == 7) {
                return cursor;
            }
            break;

        case 6:
            /* DCIMVAC c6/1, DCISW c6/2 (Figure B3-32); invalidate-entire
               c6/0 is v4/v5/v6 only (Tables D15-22, D12-8). */
            if (d->cp == 1 || d->cp == 2 || (d->cp == 0 && !v7)) {
                return cursor;
            }
            break;

        case 7:
            /* Invalidate unified cache / by MVA / by set/way, c7/{0,1,2},
               v4/v5/v6 only (Tables D15-22, D12-8; not in Figure B3-32). */
            if (!v7 && d->cp <= 2) {
                EmitMovRegImm32(cursor, kEcx, icache_self);
                EmitCall(cursor, icache_helper);
                return cursor;
            }
            break;

        case 4:
        case 8:
            /* PAR c4/0 and the ATS* address-translation ops c8/{0..7}
               (Figure B3-32); ARMv6: D12.7.12 (p. D12-2538), ARMv6K +
               Security Extensions VA-to-PA support. */
            if (v6 || v7) {
                return EmitCoprocUnimplementedFatal(cursor, d, ctx);
            }
            break;

        case 10:
            /* DCCMVAC c10/1, DCCSW c10/2 (Figure B3-32; Tables D15-22,
               D12-8); clean-entire c10/0 is ARMv6 only (Table D12-8). */
            if (d->cp == 1 || d->cp == 2 || (d->cp == 0 && v6 && !v7)) {
                return cursor;
            }
            /* CP15DSB c10/4, CP15DMB c10/5 (Table D15-23; Table D12-11,
               p. D12-2538; Figure B3-32). */
            if (d->cp == 4 || d->cp == 5) {
                return cursor;
            }
            break;

        case 11:
            /* v7: DCCMVAU c11/1 (Figure B3-32); v4/v5/v6: clean unified
               entire / by MVA / by set/way c11/{0,1,2} (Tables D15-22,
               D12-8). */
            if (v7 ? (d->cp == 1) : (d->cp <= 2)) {
                return cursor;
            }
            break;

        case 13:
            /* Prefetch icache line by MVA c13/1, v4/v5/v6 (Tables D15-22,
               D12-8); UNPREDICTABLE in ARMv7 (p. B3-1499). */
            if (d->cp == 1 && !v7) {
                return cursor;
            }
            break;

        case 14:
            /* DCCIMVAC c14/1, DCCISW c14/2 (Figure B3-32; Tables D15-22,
               D12-8); clean+invalidate-entire c14/0 is ARMv6 only
               (Table D12-8). */
            if (d->cp == 1 || d->cp == 2 || (d->cp == 0 && v6 && !v7)) {
                return cursor;
            }
            break;

        case 15:
            /* Clean and Invalidate unified cache c15/{1,2} v4/v5/v6 only
               (Tables D15-22, D12-8); entire c15/0, footnote f "Only applies
               to a unified cache" (DDI 0100I Table B6-6 p. B6-22). jornada820
               nk.exe sub_8003A3B4 0x8003A3E8 and 0x8003A430. */
            if (!v7 && (d->cp == 1 || d->cp == 2 || (d->cp == 0 && !v6))) {
                EmitMovRegImm32(cursor, kEcx, icache_self);
                EmitCall(cursor, icache_helper);
                return cursor;
            }
            break;

        default:
            break;
    }
    return EmitRaiseUndAndReturn(cursor, d, ctx);
}
