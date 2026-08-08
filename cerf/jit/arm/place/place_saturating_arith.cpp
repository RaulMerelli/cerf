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

/* SignedSatQ to 0x7FFFFFFF / 0x80000000 + APSR.Q, bit[27] (QADD
   Operation p. A8-541, B1-1148); x86 OF = signed overflow of the result
   (SDM Vol. 2A 3-33 ADD, Vol. 2B 4-682 SUB). */
uint8_t* EmitSaturateOnOverflow(uint8_t* cursor, uint8_t reg) {
    using namespace x86;
    uint8_t* no_overflow = EmitJnoLabel(cursor);
    EmitSarReg32Imm(cursor, reg, 31u);
    EmitXorRegImm32(cursor, reg, 0x80000000u);
    EmitOrBaseDisp32Imm32(cursor, kStateReg, CpsrDisp(), 1u << 27);
    FixupLabel(no_overflow, cursor);
    return cursor;
}

}  /* namespace */

/* DDI 0406C.c Table A5-8 (p. A5-202): op = insn[22:21] - QADD / QSUB /
   QDADD / QDSUB, Operations pp. A8-541 / 555 / 549 / 551: result =
   SInt(R[m]) +/- SInt(R[n]) saturated; the QD forms first saturate
   doubled = 2 * SInt(R[n]) and set Q on either stage (sat1 || sat2). */
uint8_t* PlaceSaturatingArith(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
    if (d->op1 >= 2u) {
        EmitAddReg32Reg32(cursor, kEcx, kEcx);
        cursor = EmitSaturateOnOverflow(cursor, kEcx);
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    if ((d->op1 & 0x1u) != 0u) {
        EmitSubReg32Reg32(cursor, kEax, kEcx);
    } else {
        EmitAddReg32Reg32(cursor, kEax, kEcx);
    }
    cursor = EmitSaturateOnOverflow(cursor, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}
