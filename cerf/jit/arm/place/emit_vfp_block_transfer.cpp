#include <cstdint>

#include "../arm_jit.h"
#include "../arm_vfp.h"
#include "../decoded_insn.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* VLDM / VSTM - block VFP load / store. cp_num=10 single-precision,
   cp_num=11 double-precision. Per
   references/omap3530/armv7_arch_excerpts.txt § VLDM / VSTM. */

uint8_t* EmitVfpBlockTransfer(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) {
    using namespace x86;
    ArmJit* jit = ctx->jit;

    /* Vd from D-bit + crd (= bits[15:12]). For SP form Vd = crd:D
       (5-bit single index); for DP form Vd = D:crd (5-bit double
       index). DecodedInsn stores the D bit as `n` per the LDC/STC
       decode path. */
    const bool is_dp = (d->cp_num == 11);
    const uint32_t vd = is_dp
        ? ((d->n << 4) | (d->crd & 0xFu))
        : ((d->crd << 1) | (d->n & 0x1u));

    /* MUST take abs(d->offset): the LDC/STC decoder sign-flips
       offset when U=0, so VPUSH/VSTMDB forms would otherwise
       decode imm8 as garbage and the helper UNDs spuriously. */
    const int32_t  signed_off = d->offset;
    const uint32_t abs_off    = static_cast<uint32_t>(
        signed_off < 0 ? -signed_off : signed_off);
    const uint32_t imm8       = (abs_off >> 2) & 0xFFu;

    uint32_t flags = 0;
    if (d->l) flags |= ArmVfp::kFlagL;
    if (d->w) flags |= ArmVfp::kFlagW;
    if (d->p) flags |= ArmVfp::kFlagP;
    if (is_dp) flags |= ArmVfp::kFlagDp;

    /* __cdecl PUSH RTL: flags, imm8, vd, rn, pc, vfp_ptr. */
    EmitPush32(cursor, flags);
    EmitPush32(cursor, imm8);
    EmitPush32(cursor, vd);
    EmitPush32(cursor, d->rn);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(jit->Vfp())));
    EmitCall(cursor, reinterpret_cast<void*>(
        &ArmVfp::HandleBlockTransferHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    /* EAX == 0 means success (continue block); non-zero means
       Raise* redirected state, RETN to dispatcher. */
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* continue_label = EmitJzLabel(cursor);
    EmitRetn(cursor, 0);
    FixupLabel(continue_label, cursor);
    return cursor;
}
