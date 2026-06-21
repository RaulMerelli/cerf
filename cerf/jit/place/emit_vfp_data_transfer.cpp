#include <cstdint>

#include "../decoded_insn.h"
#include "../place_fns.h"

uint8_t* EmitVfpDataTransfer(uint8_t*      cursor,
                             DecodedInsn*  d,
                             BlockContext* ctx) {
    /* VLDM / VSTM - multi-register: P=0 (IA) or P=1+W=1 (DB / VPUSH / VPOP). */
    if (d->p == 0 || (d->p == 1 && d->w == 1)) {
        return EmitVfpBlockTransfer(cursor, d, ctx);
    }
    /* VLDR / VSTR - single-register: P=1, W=0. */
    if (d->p == 1 && d->w == 0) {
        return EmitVfpSingleTransfer(cursor, d, ctx);
    }
    return EmitRaiseUndAndReturn(cursor, d, ctx);
}
