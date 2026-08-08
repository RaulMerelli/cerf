#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

constexpr int32_t CpsrDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, cpsr));
}

/* SMLA<x><y> / SMLAW<y> Operation (DDI 0406C.c pp. A8-621 / A8-631):
   signed overflow of the accumulate sets APSR.Q, bit[27] (B1-1148). */
uint8_t* EmitQOnOverflow(uint8_t* cursor) {
    using namespace x86;
    uint8_t* no_overflow = EmitJnoLabel(cursor);
    EmitOrBaseDisp32Imm32(cursor, kStateReg, CpsrDisp(), 1u << 27);
    FixupLabel(no_overflow, cursor);
    return cursor;
}

}  /* namespace */

/* DDI 0406C.c Table A5-9 (p. A5-203); operations: SMLA<x><y> p. A8-621
   (16 x 16 product + SInt(R[a]), Q on the add), SMLAW<y> A8-631
   (result<47:16> of 48-bit product + (SInt(R[a]) << 16), Q), SMULW<y>
   A8-649 (product<47:16>), SMLAL<x><y> A8-627 (sign-extended product +
   R[dHi]:R[dLo], wraps modulo 2^64), SMUL<x><y> A8-645. Halves:
   <31:16> when N / M is 1, else <15:0>, sign-extended. */
uint8_t* PlaceHalfwordMultiply(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;
    const uint32_t row = d->op1;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
    if (row != 1u) {
        if (d->n != 0u) {
            EmitSarReg32Imm(cursor, kEax, 16u);
        } else {
            EmitMovsxReg32Reg16(cursor, kEax, kEax);
        }
    }
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rm));
    if (d->u != 0u) {
        EmitSarReg32Imm(cursor, kEcx, 16u);
    } else {
        EmitMovsxReg32Reg16(cursor, kEcx, kEcx);
    }

    switch (row) {
    case 0u:
        EmitImulReg32Reg32(cursor, kEax, kEcx);
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rs));
        cursor = EmitQOnOverflow(cursor);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
        return cursor;
    case 1u:
        EmitImulReg32(cursor, kEcx);
        EmitShrdReg32Reg32Imm8(cursor, kEax, kEdx, 16u);
        if (d->s == 0u) {
            EmitAddRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rs));
            cursor = EmitQOnOverflow(cursor);
        }
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
        return cursor;
    case 2u:
        EmitImulReg32(cursor, kEcx);
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rs));
        EmitAdcRegBaseDisp32(cursor, kEdx, kStateReg, GprDisp(d->rd));
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rs), kEax);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEdx);
        return cursor;
    default:
        EmitImulReg32Reg32(cursor, kEax, kEcx);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
        return cursor;
    }
}
