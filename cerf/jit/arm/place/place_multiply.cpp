#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

}  /* namespace */

/* DDI 0406C.c Table A5-7 (p. A5-202); operations: MUL p. A8-503, MLA
   A8-481, MLS A8-483, UMAAL A8-775, UMULL A8-779, UMLAL A8-777, SMULL
   A8-647, SMLAL A8-625. The 32-bit forms' low 32 result bits "do not
   depend on whether the source register values are considered to be
   signed values or unsigned values" (A8-502); SDM Vol. 2A 3-501 states
   the same of IMUL r32, r/m32. */
uint8_t* PlaceMultiply(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rm));

    switch (d->op1) {
    case 0u:
        EmitImulReg32Reg32(cursor, kEax, kEcx);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
        if (d->s != 0u) {
            /* SDM Vol. 2A 3-502 (IMUL): "The SF, ZF, AF, and PF flags
               are undefined." */
            EmitTestRegReg(cursor, kEax, kEax);
            return EmitDpLogicalFlagTail(cursor, DpLogicalCarry::kUnchanged);
        }
        return cursor;
    case 1u:
        EmitImulReg32Reg32(cursor, kEax, kEcx);
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rs));
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
        if (d->s != 0u) {
            /* SDM Vol. 2A 3-33 (ADD): SF / ZF are set from the result. */
            return EmitDpLogicalFlagTail(cursor, DpLogicalCarry::kUnchanged);
        }
        return cursor;
    case 3u:
        EmitImulReg32Reg32(cursor, kEax, kEcx);
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rs));
        EmitSubReg32Reg32(cursor, kEcx, kEax);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEcx);
        return cursor;
    default:
        break;
    }

    if (d->op1 == 6u || d->op1 == 7u) {
        EmitImulReg32(cursor, kEcx);
    } else {
        EmitMulReg32(cursor, kEcx);
    }
    if (d->op1 == 2u) {
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rs));
        EmitAdcRegImm32(cursor, kEdx, 0u);
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rd));
        EmitAdcRegImm32(cursor, kEdx, 0u);
    } else if (d->op1 == 5u || d->op1 == 7u) {
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rs));
        EmitAdcRegBaseDisp32(cursor, kEdx, kStateReg, GprDisp(d->rd));
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rs), kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEdx);
    if (d->s == 0u) {
        return cursor;
    }
    /* A8-647: N = result<63>, Z = IsZeroBit(result<63:0>), C / V
       unchanged; ZF of the whole via OR (SDM Vol. 2B 4-173); SETcc
       (SDM Vol. 2B 4-620). */
    EmitMovRegReg(cursor, kEcx, kEdx);
    EmitOrReg32Reg32(cursor, kEcx, kEax);
    EmitSetccBaseDisp32(cursor, kSetZ, kStateReg, ArmZfDisp());
    EmitTestRegImm32(cursor, kEdx, 0x80000000u);
    EmitSetccBaseDisp32(cursor, kSetNz, kStateReg, ArmNfDisp());
    return cursor;
}
