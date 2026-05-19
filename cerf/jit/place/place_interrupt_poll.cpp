#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceInterruptPoll(uint8_t*      cursor,
                            DecodedInsn*  d,
                            BlockContext* ctx) {
    using namespace x86;

    /* MOV ECX, d->guest_address — InterruptCheck's IRQ delivery
       body reads ECX as the guest target PC. */
    EmitMovRegImm32(cursor, kEcx, d->guest_address);

    /* CALL <interrupt_check_target> — per-instance trampoline page
       wired by ArmJit::OnReady into BlockContext::interrupt_check_target.
       Byte 0 is RETN by default (no IRQ; immediate return); patched
       to NOP when an IRQ asserts. */
    EmitCall(cursor, ctx->interrupt_check_target);

    return cursor;
}
