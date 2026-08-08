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

/* ARM DDI 0406C.c Table A5-5 (p. A5-199): the 16 opcode rows AND..MVN;
   flag writeback per the A8.8 operation clauses (A8-301, A8-325);
   logical-class carry-out per A5.2.4 (p. A5-200): rotation == 0 leaves
   APSR.C unchanged, else C = imm32<31>, V unchanged. */
uint8_t* PlaceDataProcessing(uint8_t*      cursor,
                             DecodedInsn*  d,
                             BlockContext* ctx) {
    using namespace x86;

    const uint32_t opcode  = d->op1;
    const uint32_t imm32   = d->immediate;
    const bool     s       = d->s != 0;
    const bool     is_test = opcode >= 8u && opcode <= 11u;
    const bool     to_pc   = !is_test && d->rd == ArmGpr::kR15;

    const bool is_arith = (opcode >= 2u && opcode <= 7u) || opcode == 10u ||
                          opcode == 11u;
    const bool reversed = opcode == 3u || opcode == 7u;
    const bool is_move  = opcode == 13u || opcode == 15u;
    const uint32_t move_result = (opcode == 15u) ? ~imm32 : imm32;

    if (to_pc && s) {
        /* B9.3.20 (p. B9-2013): UNPREDICTABLE in User and System mode. */
        cursor = EmitSpsrModeGuard(cursor, d, ctx);
    }

    if (is_move && !to_pc) {
        EmitMovBaseDisp32Imm32(cursor, kStateReg, GprDisp(d->rd), move_result);
        if (s) {
            uint32_t bits = 0;
            uint32_t keep = 0x3FFFFFFFu;
            if (move_result & 0x80000000u) bits |= 1u << 31;
            if (move_result == 0u)         bits |= 1u << 30;
            if (d->rs != 0u) {
                keep = 0x1FFFFFFFu;
                if (imm32 & 0x80000000u) bits |= 1u << 29;
            }
            EmitAndBaseDisp32Imm32(cursor, kStateReg, CpsrDisp(), keep);
            if (bits != 0u) {
                EmitOrBaseDisp32Imm32(cursor, kStateReg, CpsrDisp(), bits);
            }
        }
        return cursor;
    }

    if (is_move) {
        EmitMovRegImm32(cursor, kEax, move_result);
    } else if (reversed) {
        if (d->rn == ArmGpr::kR15) {
            EmitMovRegImm32(cursor, kEcx, d->guest_address + 8u);
        } else {
            EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
        }
        EmitMovRegImm32(cursor, kEax, imm32);
        if (opcode == 3u) {
            EmitSubReg32Reg32(cursor, kEax, kEcx);
        } else {
            EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
            EmitCmc(cursor);
            EmitSbbReg32Reg32(cursor, kEax, kEcx);
        }
    } else {
        if (d->rn == ArmGpr::kR15) {
            EmitMovRegImm32(cursor, kEax, d->guest_address + 8u);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
        }
        switch (opcode) {
        case 0u:  EmitAndRegImm32(cursor, kEax, imm32);  break;
        case 1u:  EmitXorRegImm32(cursor, kEax, imm32);  break;
        case 2u:  EmitSubRegImm32(cursor, kEax, imm32);  break;
        case 4u:  EmitAddRegImm32(cursor, kEax, imm32);  break;
        case 5u:
            EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
            EmitAdcRegImm32(cursor, kEax, imm32);
            break;
        case 6u:
            EmitBtMemDisp32Imm8(cursor, kStateReg, CpsrDisp(), 29);
            EmitCmc(cursor);
            EmitSbbRegImm32(cursor, kEax, imm32);
            break;
        case 8u:  EmitTestRegImm32(cursor, kEax, imm32); break;
        case 9u:  EmitXorRegImm32(cursor, kEax, imm32);  break;
        case 10u: EmitCmpRegImm32(cursor, kEax, imm32);  break;
        case 11u: EmitAddRegImm32(cursor, kEax, imm32);  break;
        case 12u: EmitOrRegImm32(cursor, kEax, imm32);   break;
        case 14u: EmitAndRegImm32(cursor, kEax, ~imm32); break;
        default:
            LOG(Caution, "PlaceDataProcessing: opcode %u has no emit path "
                    "(pc=0x%08X)\n", opcode, d->guest_address);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
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

    DpLogicalCarry carry = DpLogicalCarry::kUnchanged;
    if (d->rs != 0u) {
        carry = (imm32 & 0x80000000u) ? DpLogicalCarry::kSetImm
                                      : DpLogicalCarry::kClearImm;
    }
    return EmitDpLogicalFlagTail(cursor, carry);
}
