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

}  /* namespace */

/* ARM DDI 0406C.c Table A5-3 (p. A5-197): the register rows AND..MVN.
   Logical-class carry-out = Shift_C carry, V unchanged (A8.8.14
   p. A8-327, A8.8.241 p. A8-747); arithmetic-class shifter carry is
   discarded - Shift, then AddWithCarry (A8.8.7 p. A8-313). */
uint8_t* PlaceDataProcessingReg(uint8_t*      cursor,
                                DecodedInsn*  d,
                                BlockContext* ctx) {
    using namespace x86;

    const uint32_t opcode  = d->op1;
    const uint32_t shift_t = d->n;
    const uint32_t shift_n = d->rs;
    const bool     s       = d->s != 0;
    const bool     is_test = opcode >= 8u && opcode <= 11u;
    const bool     to_pc   = !is_test && d->rd == ArmGpr::kR15;

    const bool is_arith = (opcode >= 2u && opcode <= 7u) || opcode == 10u ||
                          opcode == 11u;
    const bool reversed = opcode == 3u || opcode == 7u;
    const bool is_move  = opcode == 13u || opcode == 15u;

    if (to_pc && s) {
        /* B9.3.20 (p. B9-2013): UNPREDICTABLE in User and System mode. */
        cursor = EmitSpsrModeGuard(cursor, d, ctx);
    }

    const uint8_t shiftee = (reversed || is_move) ? kEax : kEcx;
    if (d->rm == ArmGpr::kR15) {
        /* A2.3 (p. A2-45): a PC read is the instruction address + 8. */
        EmitMovRegImm32(cursor, shiftee, d->guest_address + 8u);
    } else {
        EmitMovRegBaseDisp32(cursor, shiftee, kStateReg, GprDisp(d->rm));
    }

    /* Shift_C (A8.4.3 p. A8-293): amount == 0 leaves (value, carry_in). */
    const bool has_shifter_carry = shift_n != 0u;
    const bool capture_carry     = s && !is_arith && has_shifter_carry &&
                                   !to_pc;
    if (capture_carry) {
        EmitXorRegReg(cursor, kEdx, kEdx);
    }
    switch (shift_t) {
    case kSrLsl:
        if (shift_n != 0u) {
            EmitShlReg32Imm(cursor, shiftee, static_cast<uint8_t>(shift_n));
        }
        break;
    case kSrLsr:
        if (shift_n == 32u) {
            /* LSR_C #32 (A2.2.1 p. A2-42): result 0, carry = Rm<31>; the
               x86 count is masked mod 32 (SDM Vol. 2B 4-600), so 31 + 1. */
            EmitShrReg32Imm(cursor, shiftee, 31u);
            EmitShrReg32Imm(cursor, shiftee, 1u);
        } else {
            EmitShrReg32Imm(cursor, shiftee, static_cast<uint8_t>(shift_n));
        }
        break;
    case kSrAsr:
        if (shift_n == 32u) {
            /* ASR_C #32 (A2.2.1 p. A2-42): result and carry = Rm<31>. */
            EmitSarReg32Imm(cursor, shiftee, 31u);
            EmitSarReg32Imm(cursor, shiftee, 1u);
        } else {
            EmitSarReg32Imm(cursor, shiftee, static_cast<uint8_t>(shift_n));
        }
        break;
    case kSrRor:
        /* ROR_C (A2.2.1 p. A2-42): carry = result<31> = the x86 ROR CF
           (SDM Vol. 2B 4-533). */
        EmitRorReg32Imm(cursor, shiftee, static_cast<uint8_t>(shift_n));
        break;
    default:
        /* RRX_C (A2.2.1 p. A2-43): result = carry_in : Rm<31:1>,
           carry = Rm<0> - RCR by 1 with CF = CPSR.C. */
        EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
        EmitRcrReg32By1(cursor, shiftee);
        break;
    }
    if (capture_carry) {
        EmitSetcReg8(cursor, kDl);
    }

    if (!is_move) {
        const uint8_t other = reversed ? kEcx : kEax;
        if (d->rn == ArmGpr::kR15) {
            EmitMovRegImm32(cursor, other, d->guest_address + 8u);
        } else {
            EmitMovRegBaseDisp32(cursor, other, kStateReg, GprDisp(d->rn));
        }
    }

    switch (opcode) {
    case 0u:  EmitAndReg32Reg32(cursor, kEax, kEcx); break;
    case 1u:
    case 9u:  EmitXorRegReg(cursor, kEax, kEcx);     break;
    case 2u:
    case 3u:
    case 10u: EmitSubReg32Reg32(cursor, kEax, kEcx); break;
    case 4u:
    case 11u: EmitAddReg32Reg32(cursor, kEax, kEcx); break;
    case 5u:
        EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
        EmitAdcReg32Reg32(cursor, kEax, kEcx);
        break;
    case 6u:
    case 7u:
        /* SBC / RSC (A8.8.162 p. A8-595 / A8.8.156 p. A8-583):
           AddWithCarry(x, NOT(y), APSR.C) subtracts NOT(C) where the x86
           SBB subtracts CF (SDM Vol. 2B 4-608) - CMC inverts. */
        EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
        EmitCmc(cursor);
        EmitSbbReg32Reg32(cursor, kEax, kEcx);
        break;
    case 8u:  EmitTestRegReg(cursor, kEax, kEcx);    break;
    case 12u: EmitOrReg32Reg32(cursor, kEax, kEcx);  break;
    case 13u:
        if (s && !to_pc) {
            EmitTestRegReg(cursor, kEax, kEax);
        }
        break;
    case 14u:
        /* BIC (A8.8.22 p. A8-343): Rn AND NOT(shifted); NOT leaves the
           flags, the captured shifter carry included (SDM Vol. 2B
           4-170). */
        EmitNotReg32(cursor, kEcx);
        EmitAndReg32Reg32(cursor, kEax, kEcx);
        break;
    default:
        /* MVN (A8.8.116 p. A8-507): NOT(shifted). */
        EmitNotReg32(cursor, kEax);
        if (s && !to_pc) {
            EmitTestRegReg(cursor, kEax, kEax);
        }
        break;
    }

    if (to_pc) {
        return EmitDpPcWriteTail(cursor, d, ctx);
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

    return EmitDpLogicalFlagTail(cursor,
                                 has_shifter_carry ? DpLogicalCarry::kFromDl
                                                   : DpLogicalCarry::kUnchanged);
}
