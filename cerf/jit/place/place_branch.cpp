#include <cstddef>

#include "../arm_jit.h"
#include "../arm_jit_runtime.h"
#include "../arm_mmu.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceBranch(uint8_t*      cursor,
                     DecodedInsn*  d,
                     BlockContext* ctx) {
    using namespace x86;

    /* BL: write LR = guest_pc + 4 (instruction-after-BL) and push
       the return address onto the per-instance shadow stack so a
       future BX LR / MOV PC, LR can JMP straight to the cached
       host code without round-tripping through R15ModifiedHelper. */
    if (d->l) {
        EmitMovBaseDisp32Imm32(cursor, kStateReg,
                               static_cast<int32_t>(offsetof(ArmCpuState, gprs) + 14 * 4),
                               d->guest_address + 4u);
        cursor = PlacePushShadowStack(cursor, d, ctx);
    }

    if (d->reserved3 <= d->guest_address) {
        cursor = PlaceInterruptPoll(cursor, d, ctx);
    }

    /* Destination inside the current compile batch AND it's a
       known entrypoint? Reserve 5 bytes for a back-patched JMP
       rel32; JitApplyFixups fills it in once every entrypoint's
       native_start is finalized. */
    if (d->reserved3 >= ctx->insns[0].guest_address &&
        d->reserved3 <= ctx->insns[ctx->num_insns - 1].guest_address) {
        const uint32_t instruction_size =
            ctx->jit->CpuState()->cpsr.bits.thumb_mode ? 2u : 4u;
        const uint32_t off =
            (d->reserved3 - ctx->insns[0].guest_address) / instruction_size;

        if (ctx->insns[off].entry_point->guest_start ==
            ApplyFcseFold(*ctx->jit->Mmu()->State(), d->reserved3)) {
            d->jmp_fixup_location = cursor;
            cursor += 5;  /* JMP rel32 placeholder filled by JitApplyFixups */
            return cursor;
        }
    }

    /* Optimistic emit-time lookup: if the destination is already
       translated, emit a direct JMP rel32 to its native_start. */
    JitBlock* ep =
        ArmJit::FindBlockExactHelper(ctx->jit, static_cast<uint32_t>(d->offset));
    if (ep) {
        EmitJmp32(cursor, ep->native_start);
    } else {
        /* Generic runtime path: MOV ECX, dest; CALL branch_helper.
           Branch helper resolves at runtime, self-patches this
           10-byte sequence on hit so subsequent executions skip
           the trampoline. */
        EmitMovRegImm32(cursor, kEcx, d->reserved3);
        EmitCall(cursor, ctx->branch_helper_target);
    }
    return cursor;
}
