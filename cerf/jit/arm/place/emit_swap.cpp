#include <cstddef>
#include <cstdint>

#include "../../../cpu/arm_processor_config.h"
#include "../arm_jit.h"
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

/* SWP / SWPB - ARM ARM DDI 0406C.c A8.8.229 (pp. A8-722/A8-723). B is insn
   bit 22 (DecodedInsn::n), Rt2 is insn[3:0] (DecodedInsn::rm). */
uint8_t* EmitSwap(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    /* A8.8.229: t == 15 || t2 == 15 || n == 15 || n == t || n == t2 is
       UNPREDICTABLE -> UNDEFINED (p. Glossary-2737). */
    if (d->rd == 15 || d->rm == 15 || d->rn == 15 ||
        d->rn == d->rd || d->rn == d->rm) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const ArmProcessorConfig* config = ctx->jit->ProcessorConfig();
    ArmMmu*                   mmu    = ctx->jit->Mmu();
    const ArmSctlr            sctlr  = mmu->State()->control_register;
    /* Unaligned SWP faults when U == 1 (Table A3-1, p. A3-108; D12.3.1,
       p. D12-2506) or A == 1 (D15.3.1, p. D15-2592); U by architecture
       version per Table D12-1 (p. D12-2506). */
    const bool u1 = config->HasCp15V7() || (config->HasCp15V6() && sctlr.bits.u);
    const bool align_fault_check = !d->n && (u1 || sctlr.bits.a);

    uint8_t* align_fault_label = nullptr;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
    if (align_fault_check) {
        EmitTestRegImm32(cursor, kEcx, 3u);
        align_fault_label = EmitJnzLabel32(cursor);
    } else if (!d->n) {
        EmitAndRegImm32(cursor, kEcx, 0xFFFFFFFCu);
    }
    cursor = EmitTlbFastPath(cursor, ctx, TlbAccess::kReadWrite);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* abort_label = EmitJzLabel32(cursor);

    if (d->n) {
        /* MOVZX EDX, byte [EAX] - 0F B6 /r (SDM Vol. 2B 4-140 MOVZX). */
        Emit8(cursor, 0x0F);
        Emit8(cursor, 0xB6);
        EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);

        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rm));
        /* MOV [EAX], CL - 88 /r (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x88);
        EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kCl);
    } else {
        /* MOV EDX, [EAX] - 8B /r (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x8B);
        EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);

        if (!align_fault_check) {
            /* CL = 8 * address<1:0> (D15.3.1, p. D15-2592). */
            EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
            EmitAndRegImm32(cursor, kEcx, 3u);
            EmitShlReg32Imm(cursor, kEcx, 3);
            /* ROR EDX, CL - D3 /1 (SDM Vol. 2B 4-533 RCL/RCR/ROL/ROR). */
            Emit8(cursor, 0xD3);
            EmitModRmReg(cursor, /*mod=*/3, /*rm=*/kEdx, /*reg=*/1);
        }

        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rm));
        /* MOV [EAX], ECX - 89 /r (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x89);
        EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEcx);
    }

    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEdx);
    uint8_t* done_label = EmitJmpLabel32(cursor);

    /* .align_fault: ECX = EA; the pseudocode's aborting access is the
       load (A8.8.229: data = MemA[R[n]] precedes the store). */
    if (align_fault_label != nullptr) {
        FixupLabel32(align_fault_label, cursor);
        EmitMovRegImm32(cursor, kEdx,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mmu)));
        EmitCall(cursor,
                 reinterpret_cast<void*>(&ArmMmu::AlignmentFaultReadHelper));
    }

    /* .abort: */
    FixupLabel32(abort_label, cursor);
    cursor = EmitAbortDataTail(cursor, d, ctx);

    /* .done: */
    FixupLabel32(done_label, cursor);
    return cursor;
}
