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

/* A2.2.1 (pp. A2-41/42): LSL_C / LSR_C carry = the last bit shifted out,
   zero for amounts past 32; #32 carry = Rm<0> / Rm<31> with a zero
   result; ASR_C amounts >= 32 give the sign in every bit and the carry;
   ROR_C carry = result<31> with the amount taken MOD 32; Shift_C
   (A8.4.3 p. A8-293): amount == 0 leaves (value, carry_in). x86: a
   CL count of 0 leaves the flags unaffected (SDM Vol. 2B 4-602), ROR
   with a masked count of 0 leaves the flags unaffected (4-535), and
   shift counts are masked to 5 bits (4-600). */
uint8_t* EmitShiftByClCapture(uint8_t* cursor, uint32_t type) {
    using namespace x86;

    EmitTestRegReg(cursor, kEcx, kEcx);
    uint8_t* zero_l = EmitJzLabel32(cursor);
    uint8_t* done_a = nullptr;
    uint8_t* done_b = nullptr;
    switch (type) {
    case 0u:
    case 1u: {
        EmitCmpRegImm8(cursor, kEcx, 32);
        uint8_t* small_l = EmitJbLabel32(cursor);
        uint8_t* eq_l    = EmitJzLabel32(cursor);
        EmitXorRegReg(cursor, kEax, kEax);
        done_a = EmitJmpLabel32(cursor);
        FixupLabel32(eq_l, cursor);
        EmitBtRegImm8(cursor, kEax, type == 0u ? 0u : 31u);
        EmitMovRegImm32(cursor, kEax, 0u);
        done_b = EmitJmpLabel32(cursor);
        FixupLabel32(small_l, cursor);
        if (type == 0u) {
            EmitShlReg32Cl(cursor, kEax);
        } else {
            EmitShrReg32Cl(cursor, kEax);
        }
        break;
    }
    case 2u: {
        EmitCmpRegImm8(cursor, kEcx, 32);
        uint8_t* small_l = EmitJbLabel32(cursor);
        EmitSarReg32Imm(cursor, kEax, 31u);
        EmitSarReg32Imm(cursor, kEax, 1u);
        done_a = EmitJmpLabel32(cursor);
        FixupLabel32(small_l, cursor);
        EmitSarReg32Cl(cursor, kEax);
        break;
    }
    default: {
        EmitTestRegImm32(cursor, kEcx, 31u);
        uint8_t* ror_l = EmitJnzLabel32(cursor);
        EmitBtRegImm8(cursor, kEax, 31u);
        done_a = EmitJmpLabel32(cursor);
        FixupLabel32(ror_l, cursor);
        EmitRorReg32Cl(cursor, kEax);
        break;
    }
    }
    uint8_t* done_c = EmitJmpLabel32(cursor);
    FixupLabel32(zero_l, cursor);
    EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
    FixupLabel32(done_a, cursor);
    if (done_b != nullptr) FixupLabel32(done_b, cursor);
    FixupLabel32(done_c, cursor);
    EmitSetcReg8(cursor, kDl);
    return cursor;
}

uint8_t* EmitShiftByCl(uint8_t* cursor, uint32_t type) {
    using namespace x86;

    switch (type) {
    case 0u:
    case 1u: {
        EmitCmpRegImm8(cursor, kEcx, 32);
        uint8_t* small_l = EmitJbLabel32(cursor);
        EmitXorRegReg(cursor, kEax, kEax);
        uint8_t* done_l = EmitJmpLabel32(cursor);
        FixupLabel32(small_l, cursor);
        if (type == 0u) {
            EmitShlReg32Cl(cursor, kEax);
        } else {
            EmitShrReg32Cl(cursor, kEax);
        }
        FixupLabel32(done_l, cursor);
        break;
    }
    case 2u: {
        EmitCmpRegImm8(cursor, kEcx, 32);
        uint8_t* small_l = EmitJbLabel32(cursor);
        EmitMovRegImm32(cursor, kEcx, 31u);
        FixupLabel32(small_l, cursor);
        EmitSarReg32Cl(cursor, kEax);
        break;
    }
    default:
        EmitRorReg32Cl(cursor, kEax);
        break;
    }
    return cursor;
}

}  /* namespace */

/* ARM DDI 0406C.c Table A5-4 (p. A5-198): the register-shifted rows
   AND..MVN; shift amount = UInt(R[s]<7:0>) (A8.8.15 Operation p. A8-329).
   Logical-class carry-out = Shift_C carry, V unchanged (A8-329, A8.8.242
   p. A8-749); arithmetic-class shifter carry is discarded - Shift, then
   AddWithCarry (A8.8.3 p. A8-305). */
uint8_t* PlaceDataProcessingShiftedReg(uint8_t*      cursor,
                                       DecodedInsn*  d,
                                       BlockContext*) {
    using namespace x86;

    const uint32_t opcode  = d->op1;
    const uint32_t type    = d->n;
    const bool     s       = d->s != 0;
    const bool     is_test = opcode >= 8u && opcode <= 11u;

    const bool is_arith = (opcode >= 2u && opcode <= 7u) || opcode == 10u ||
                          opcode == 11u;
    const bool is_move = opcode == 13u || opcode == 15u;
    const bool capture = s && !is_arith;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rs));
    EmitAndRegImm32(cursor, kEcx, 0xFFu);
    if (capture) {
        EmitXorRegReg(cursor, kEdx, kEdx);
        cursor = EmitShiftByClCapture(cursor, type);
    } else {
        cursor = EmitShiftByCl(cursor, type);
    }

    if (!is_move) {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
    }

    switch (opcode) {
    case 0u:  EmitAndReg32Reg32(cursor, kEax, kEcx); break;
    case 1u:
    case 9u:  EmitXorRegReg(cursor, kEax, kEcx);     break;
    case 2u:
        EmitSubReg32Reg32(cursor, kEcx, kEax);
        EmitMovRegReg(cursor, kEax, kEcx);
        break;
    case 10u: EmitSubReg32Reg32(cursor, kEcx, kEax); break;
    case 3u:  EmitSubReg32Reg32(cursor, kEax, kEcx); break;
    case 4u:
    case 11u: EmitAddReg32Reg32(cursor, kEax, kEcx); break;
    case 5u:
        EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
        EmitAdcReg32Reg32(cursor, kEax, kEcx);
        break;
    case 6u:
        /* SBC (A8.8.163 p. A8-597): AddWithCarry(Rn, NOT(shifted),
           APSR.C) subtracts NOT(C) where the x86 SBB subtracts CF
           (SDM Vol. 2B 4-608) - CMC inverts. */
        EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
        EmitCmc(cursor);
        EmitSbbReg32Reg32(cursor, kEcx, kEax);
        EmitMovRegReg(cursor, kEax, kEcx);
        break;
    case 7u:
        /* RSC (A8.8.157 p. A8-585): AddWithCarry(NOT(Rn), shifted,
           APSR.C). */
        EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
        EmitCmc(cursor);
        EmitSbbReg32Reg32(cursor, kEax, kEcx);
        break;
    case 8u:  EmitTestRegReg(cursor, kEax, kEcx);    break;
    case 12u: EmitOrReg32Reg32(cursor, kEax, kEcx);  break;
    case 13u:
        if (s) {
            EmitTestRegReg(cursor, kEax, kEax);
        }
        break;
    case 14u:
        /* BIC (A8.8.23 Operation, p. A8-345): Rn AND NOT(shifted); NOT
           leaves the flags (SDM Vol. 2B 4-170). */
        EmitNotReg32(cursor, kEax);
        EmitAndReg32Reg32(cursor, kEax, kEcx);
        break;
    default:
        /* MVN (A8.8.117 p. A8-509): NOT(shifted). */
        EmitNotReg32(cursor, kEax);
        if (s) {
            EmitTestRegReg(cursor, kEax, kEax);
        }
        break;
    }

    if (!is_test) {
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    }
    if (!s) {
        return cursor;
    }
    if (is_arith) {
        return EmitDpArithFlagTail(cursor, d);
    }
    return EmitDpLogicalFlagTail(cursor, DpLogicalCarry::kFromDl);
}
