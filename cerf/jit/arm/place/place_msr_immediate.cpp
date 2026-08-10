#include <cstdint>

#include "../arm_emit_services.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../../core/log.h"
#include "../../../cpu/arm_processor_config.h"

uint8_t* PlaceMSRImmediate(uint8_t*      cursor,
                           DecodedInsn*  d,
                           BlockContext* ctx) {
    using namespace x86;

    /* mask == 0 on ARMv7 only: R == 0 selects the hints space (ARM DDI
       0406C.c A5.2.11, p. A5-206), R == 1 is UNPREDICTABLE (B9.3.11,
       p. B9-1996). Pre-v7, DDI 0100I A4.1.39 (p. A4-77) merges
       byte_mask = 0 as a PSR-unchanged no-op through the normal path. */
    if (d->crn == 0u && ctx->emit->ProcessorConfig()->HasCp15V7()) {
        if (d->n == 0u) {
            LOG(Jit, "PlaceMSRImmediate: hint encoding (mask==0, R==0) "
                     "routed here at pc=0x%08X\n", d->guest_address);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* operand = 8_bit_immediate ROR (rotate_imm * 2) - ARM DDI 0100I
       A4.1.39 (p. A4-76); ARM DDI 0406C.c A8.8.111 (p. A8-498). */
    const uint32_t imm8  = d->immediate & 0xFFu;
    const uint32_t rot   = ((d->immediate >> 8) & 0xFu) * 2u;
    const uint32_t value =
        rot ? ((imm8 >> rot) | (imm8 << (32u - rot))) : imm8;

    EmitMovRegImm32(cursor, kEax, value);
    return EmitMsrWriteTail(cursor, d, ctx);
}
