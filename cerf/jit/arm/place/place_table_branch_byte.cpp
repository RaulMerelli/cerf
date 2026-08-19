#include <cstddef>

#include "../block_context.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

}

/* ARM DDI 0406C.c A8.8.236 TBB, TBH Operation (p. A8-737): "halfwords =
   UInt(MemU[R[n]+R[m], 1]); BranchWritePC(PC + 2*halfwords);". BranchWritePC
   (p. A2-47) is "BranchTo(address<31:1>:'0')" in Thumb state. */
uint8_t* PlaceTableBranchByte(uint8_t* cursor, DecodedInsn* d,
                              BlockContext* ctx) {
    using namespace x86;

    if (d->rn == 15u) {
        EmitMovRegImm32(cursor, kEcx, ArmPcReadValue(d, ctx));
    } else {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
    }
    EmitAddRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rm));

    cursor = EmitTranslateAccess(cursor, ctx, TlbAccess::kRead, false);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* abort_label = EmitJzLabel32(cursor);

    /* MOVZX EDX, byte [EAX] - 0F B6 /r (SDM Vol. 2B 4-140 MOVZX). */
    Emit8(cursor, 0x0F);
    Emit8(cursor, 0xB6);
    EmitModRmReg(cursor, 0, kEax, kEdx);

    EmitShlReg32Imm(cursor, kEdx, 1u);
    EmitAddRegImm32(cursor, kEdx, ArmPcReadValue(d, ctx));
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(15u), kEdx);
    cursor = PlaceR15ModifiedHelper(cursor, d, ctx);

    FixupLabel32(abort_label, cursor);
    return EmitAbortDataTail(cursor, d, ctx);
}
