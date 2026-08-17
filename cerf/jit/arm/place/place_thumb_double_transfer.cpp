#include <cstddef>

#include "../block_context.h"
#include "../arm_emit_services.h"
#include "../arm_mmu.h"
#include "../arm_mmu_state.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

namespace {
constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}
}

/* ARM DDI 0406C.c A8.8.72/.210 LDRD/STRD T1: Thumb names Rt2 explicitly;
   d->rs carries Rt2 and offset is imm8:'00'. */
uint8_t* PlaceThumbDoubleTransfer(uint8_t* cursor, DecodedInsn* d,
                                  BlockContext* ctx) {
    using namespace x86;
    ArmMmu* const mmu = ctx->emit->Mmu();
    const ArmSctlr sctlr = mmu->State()->effective_control_register;
    const uint32_t mmu_imm =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mmu));

    if (d->rn == ArmGpr::kR15) {
        EmitMovRegImm32(cursor, kEcx, ArmPcReadValue(d, ctx));
    } else {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
    }
    if (d->p && d->offset != 0) {
        if (d->u) EmitAddRegImm32(cursor, kEcx, static_cast<uint32_t>(d->offset));
        else EmitSubRegImm32(cursor, kEcx, static_cast<uint32_t>(d->offset));
    }

    /* ARM DDI 0406C.c Table A3-1 (p. A3-108), D12.3.1 (p. D12-2506),
       and D15.3.1 (p. D15-2592): the modified 6.6 implementation selected
       an 8-byte-aligned address when SCTLR.A was clear and raised an
       Alignment fault otherwise. Preserve that guest-visible behavior. */
    uint8_t* align_fault = nullptr;
    if (sctlr.bits.a) {
        EmitTestRegImm32(cursor, kEcx, 7u);
        align_fault = EmitJnzLabel32(cursor);
    } else {
        EmitAndRegImm32(cursor, kEcx, 0xFFFFFFF8u);
    }

    cursor = EmitTlbFastPath(cursor, ctx,
                             d->l ? TlbAccess::kRead : TlbAccess::kWrite);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* abort = EmitJzLabel32(cursor);
    if (d->l) {
        EmitMovRegBaseDisp32(cursor, kEdx, kEax, 0);
        EmitMovRegBaseDisp32(cursor, kEcx, kEax, 4);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEdx);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rs), kEcx);
    } else {
        EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, GprDisp(d->rd));
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rs));
        EmitMovBaseDisp32Reg(cursor, kEax, 0, kEdx);
        EmitMovBaseDisp32Reg(cursor, kEax, 4, kEcx);
    }
    if (d->w) {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
        if (d->u) EmitAddRegImm32(cursor, kEcx, static_cast<uint32_t>(d->offset));
        else EmitSubRegImm32(cursor, kEcx, static_cast<uint32_t>(d->offset));
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rn), kEcx);
    }
    uint8_t* done = EmitJmpLabel32(cursor);

    if (align_fault != nullptr) {
        FixupLabel32(align_fault, cursor);
        EmitMovRegImm32(cursor, kEdx, mmu_imm);
        EmitCall(cursor, d->l
            ? reinterpret_cast<void*>(&ArmMmu::AlignmentFaultReadHelper)
            : reinterpret_cast<void*>(&ArmMmu::AlignmentFaultWriteHelper));
    }
    FixupLabel32(abort, cursor);
    cursor = EmitAbortDataTail(cursor, d, ctx);
    FixupLabel32(done, cursor);
    return cursor;
}
