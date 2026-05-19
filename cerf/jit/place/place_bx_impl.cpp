#include <cstddef>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceBxImpl(uint8_t*      cursor,
                     DecodedInsn*  d,
                     BlockContext* ctx,
                     bool          is_call) {
    using namespace x86;

#if CERF_DEV_MODE
    if (d->guest_address == 0x8C0517B4u) {
        LOG(Jit, "PlaceBxImpl emit BLX 0x8C0517B4 is_call=%d Rd=R%u cursor=%p\n",
            is_call, d->rd, (void*)cursor);
    }

    /* Emit a runtime trace that fires AFTER the R15-write to confirm
       state.gprs[15] is what we expect just before the helper JMP. */
    auto emit_trace_after_r15_write = [&](uint8_t*& c) {
        if (d->guest_address != 0x8C0517B4u) return;
        /* CDECL: void trace(uint32_t r15_value).
           At this point in the emit, EAX holds the value we just
           wrote to state.gprs[15]. */
        EmitPushReg(c, kEax);
        EmitCall   (c, reinterpret_cast<void*>(
            +[](uint32_t r15_value) {
                LOG(Jit, "BLX 0x8C0517B4 post-R15-write: EAX=0x%08X\n",
                    r15_value);
            }));
        EmitAddRegImm32(c, kEsp, 4);
    };
#else
    auto emit_trace_after_r15_write = [](uint8_t*&) {};
#endif

    /* MOV EAX, [ESI + GPRs[Rd]] */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4));

    /* TEST EAX, 1 */
    EmitTestRegImm32(cursor, kEax, 1);
    uint8_t* jnz_switch_to_thumb = EmitJnzLabel(cursor);

    /* ARM-mode path: MOV [ESI + GPRs[15]], EAX */
    EmitMovBaseDisp32Reg(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + 15 * 4),
        kEax);

    emit_trace_after_r15_write(cursor);

    if (is_call || d->rd == ArmGpr::kR15 || d->rd == 12) {
        cursor = PlaceR15ModifiedHelper(cursor, d, ctx);
    } else {
        /* BX LR or similar return — try the shadow-stack pop. */
        EmitJmp32(cursor, ctx->pop_shadow_stack_helper_target);
    }

    /* SwitchToThumb: */
    FixupLabel(jnz_switch_to_thumb, cursor);

    /* OR [ESI + cpsr], 0x20 — set CPSR.T (Thumb). cpsr is at
       offset offsetof(cpsr); the T bit is bit 5 of CPSR.partial_word. */
    Emit8(cursor, 0x81);
    EmitModRmReg(cursor, 2, kStateReg, 1);  /* mod=10 r/m=ESI reg=1 (OR ext) */
    Emit32(cursor, static_cast<uint32_t>(offsetof(ArmCpuState, cpsr)));
    Emit32(cursor, 0x00000020u);

    EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);

    /* MOV [ESI + GPRs[15]], EAX */
    EmitMovBaseDisp32Reg(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + 15 * 4),
        kEax);

    if (is_call || d->rd == ArmGpr::kR15 || d->rd == 12) {
        cursor = PlaceR15ModifiedHelper(cursor, d, ctx);
    } else {
        EmitJmp32(cursor, ctx->pop_shadow_stack_helper_target);
    }

    return cursor;
}
