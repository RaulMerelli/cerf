#include <cstddef>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceEntrypointEnd(uint8_t*      cursor,
                            DecodedInsn*  d,
                            BlockContext* ctx) {
    using namespace x86;

    /* Deliver pending interrupts before exiting the block. */
    cursor = PlaceInterruptPoll(cursor, d, ctx);

    /* update_location is the byte address where the self-modifying
       JMP-rel32 will be patched in by EntrypointEndHelper at
       runtime if the next entrypoint becomes available. */
    uint8_t* update_location = cursor;

    /* Optimistic pre-resolve at emit time: if the next guest
       address is already translated, emit a direct JMP straight to
       its native_start with no runtime lookup. ctx->jit gives us
       the ArmJit instance for the per-instance index lookup. */
    JitBlock* ep = ArmJit::FindBlockExactHelper(ctx->jit, d->guest_address);

    if (ep) {
        /* Direct JMP to known native_start. */
        EmitJmp32(cursor, ep->native_start);
    } else {
        EmitPush32(cursor, d->guest_address);              /* arg2: guest_pc */
        EmitPush32(cursor,
                   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
                                                            /* arg1: jit */
        EmitCall(cursor, reinterpret_cast<void*>(&ArmJit::FindBlockExactHelper));
        EmitAddRegImm32(cursor, kEsp, 8);

        EmitTestRegReg(cursor, kEax, kEax);
        uint8_t* not_in_cache = EmitJzLabel(cursor);

        EmitMovRegImm32(cursor, kEdi,
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(update_location)));
        EmitJmp32(cursor, reinterpret_cast<void*>(&ArmJit::EntrypointEndHelper));

        /* not_in_cache: write d->guest_address into ArmCpuState->gprs[15]
           and RETN to dispatcher. ESI is pinned to ArmCpuState* so
           [ESI + offsetof(gprs[15])] addresses the slot. */
        FixupLabel(not_in_cache, cursor);
        EmitMovBaseDisp32Imm32(cursor, kStateReg,
                               static_cast<int32_t>(offsetof(ArmCpuState, gprs) + 15 * 4),
                               d->guest_address);
        EmitRetn(cursor, 0);
    }

    return cursor;
}
