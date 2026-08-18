#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

}

/* ARM DDI 0406C.c A8.8.29 CBNZ, CBZ Operation (p. A8-357): "if nonzero !=
   IsZero(R[n]) then BranchWritePC(PC + imm32);", with "nonzero = (op == '1')"
   from encoding T1 (p. A8-356). */
uint8_t* PlaceCbz(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* not_taken = d->op1 != 0u ? EmitJzLabel32(cursor)
                                      : EmitJnzLabel32(cursor);
    cursor = PlaceBranch(cursor, d, ctx);
    FixupLabel32(not_taken, cursor);
    return cursor;
}
