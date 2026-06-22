#include <cstdint>

#include "../arm_jit.h"
#include "../arm_vfp.h"
#include "../decoded_insn.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* VLDR / VSTR - single-register VFP load/store. Encoding form
   P=1, W=0; cp_num=10 SP, cp_num=11 DP. Per
   references/omap3530/armv7_arch_excerpts.txt § VLDR / VSTR. */

uint8_t* EmitVfpSingleTransfer(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) {
    using namespace x86;
    ArmJit* jit = ctx->jit;

    const bool is_dp = (d->cp_num == 11);
    const uint32_t vd = is_dp
        ? ((d->n << 4) | (d->crd & 0xFu))
        : ((d->crd << 1) | (d->n & 0x1u));

    uint32_t flags = 0;
    if (d->l)  flags |= ArmVfp::kFlagL;
    if (is_dp) flags |= ArmVfp::kFlagDp;

    /* __cdecl PUSH RTL: flags, signed_off, vd, rn, pc, vfp_ptr. */
    EmitPush32(cursor, flags);
    EmitPush32(cursor, static_cast<uint32_t>(d->offset));
    EmitPush32(cursor, vd);
    EmitPush32(cursor, d->rn);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(jit->Vfp())));
    EmitCall(cursor, reinterpret_cast<void*>(
        &ArmVfp::HandleSingleTransferHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* continue_label = EmitJzLabel(cursor);
    EmitRetn(cursor, 0);
    FixupLabel(continue_label, cursor);
    return cursor;
}
