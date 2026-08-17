#include <cstddef>

#include "../block_context.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

namespace {
constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}
}

/* ARM DDI 0406C.c A8.8.188/.189 TBB/TBH (pp. A8-652/A8-654):
   BranchWritePC(PC + 2 * table_data). */
uint8_t* PlaceTableBranch(uint8_t* cursor, DecodedInsn* d,
                          BlockContext* ctx) {
    using namespace x86;
    if (d->rn == ArmGpr::kR15) {
        EmitMovRegImm32(cursor, kEcx, ArmPcReadValue(d, ctx));
    } else {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    if (d->s != 0u) EmitShlReg32Imm(cursor, kEax, 1u);
    EmitAddReg32Reg32(cursor, kEcx, kEax);
    cursor = EmitTlbFastPath(cursor, ctx, TlbAccess::kRead);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* abort = EmitJzLabel32(cursor);
    Emit8(cursor, 0x0F);
    Emit8(cursor, d->s != 0u ? 0xB7 : 0xB6);  /* MOVZX EDX, word/byte [EAX]. */
    EmitModRmReg(cursor, 0, kEax, kEdx);
    EmitShlReg32Imm(cursor, kEdx, 1u);
    EmitAddRegImm32(cursor, kEdx, d->guest_address + 4u);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(ArmGpr::kR15), kEdx);
    uint8_t* done = EmitJmpLabel32(cursor);
    FixupLabel32(abort, cursor);
    cursor = EmitAbortDataTail(cursor, d, ctx);
    FixupLabel32(done, cursor);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}
