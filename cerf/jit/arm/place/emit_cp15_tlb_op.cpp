#include <cstddef>
#include <cstdint>

#include "../../../cpu/arm_processor_config.h"
#include "../arm_emit_services.h"
#include "../arm_translation_cache.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* ARM ARM DDI 0406C.c Figure B3-34 (p. B3-1476), ARMv7 CP15 c8; D12.7.13
   (p. D12-2539): ARMv6 provision = ARMv7-A; Table D15-24 (p. D15-2631),
   ARMv4/v5: CRm c5/c6/c7 with opc2 0/1 only. */
uint8_t* EmitCp15TlbOp(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;
    const bool v6plus = emit->ProcessorConfig()->HasCp15V6() ||
                        emit->ProcessorConfig()->HasCp15V7();

    /* Figure B3-34: every c8 op is write-only with opc1=0; not-shown and
       unimplemented-extension encodings UNPREDICTABLE (p. B3-1476) ->
       UNDEFINED (p. Glossary-2737). */
    if (d->l || d->cp_opc != 0) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    void* all_helper = nullptr;
    void* mva_helper = nullptr;
    switch (d->crm) {
        case 5:
            all_helper = reinterpret_cast<void*>(
                &ArmTranslationCache::ItlbInvalidateAllHelper);
            mva_helper = reinterpret_cast<void*>(
                &ArmTranslationCache::ItlbInvalidateMvaHelper);
            break;
        case 6:
            all_helper = reinterpret_cast<void*>(
                &ArmTranslationCache::DtlbInvalidateAllHelper);
            mva_helper = reinterpret_cast<void*>(
                &ArmTranslationCache::DtlbInvalidateMvaHelper);
            break;
        case 7:
            all_helper = reinterpret_cast<void*>(
                &ArmTranslationCache::UtlbInvalidateAllHelper);
            mva_helper = reinterpret_cast<void*>(
                &ArmTranslationCache::UtlbInvalidateMvaHelper);
            break;
        default:
            return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    switch (d->cp) {
        case 0:
            EmitMovRegImm32(cursor, kEcx,
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(emit->TranslationCache())));
            EmitCall(cursor, all_helper);
            return cursor;
        case 1:
            EmitMovRegBaseDisp32(cursor, kEcx, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u));
            EmitMovRegImm32(cursor, kEdx,
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(emit->TranslationCache())));
            EmitCall(cursor, mva_helper);
            return cursor;
        case 2:
            /* By-ASID invalidate, ARMv6+ only (Figure B3-34, p. B3-1476);
               dropped as a whole-unit flush per B3.10.1 (p. B3-1381): "Any
               TLB operation can affect any other TLB entries that are not
               locked down." */
            if (v6plus) {
                EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(emit->TranslationCache())));
                EmitCall(cursor, all_helper);
                return cursor;
            }
            return EmitRaiseUndAndReturn(cursor, d, ctx);
        default:
            return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
}
