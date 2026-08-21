#include "../decoded_insn.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

uint8_t* PlaceR15ModifiedHelper(uint8_t* cursor, DecodedInsn* d,
                                BlockContext* ctx) {
    /* DDI 0406C.c A2.5.2 (p. A2-52): normal execution of a branching
       instruction "always results in ITSTATE advancing to normal execution".
       B9.3.19 SUBS PC, LR (Thumb) (p. B9-2010) instead "copies the SPSR to the
       CPSR", which B1.3.3 (p. B1-1148) places IT[7:0] inside. */
    if (d->itstate_after_valid != 0u && d->is_exception_return == 0u) {
        cursor = EmitItStateStore(cursor, d->itstate_after);
    }
    /* QEMU target/arm/tcg/translate.c gen_rfe and the trans_LDM exc_return
       tail: "Must exit loop to check un-masked IRQs" - DISAS_EXIT, never
       gen_goto_ptr. */
    if (d->is_exception_return == 0u) {
        cursor = EmitJumpCacheProbe(cursor, ctx);
    }
    x86::EmitRet(cursor);
    return cursor;
}
