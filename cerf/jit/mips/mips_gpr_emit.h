#pragma once

#include <cstddef>
#include <cstdint>

#include "mips_cpu_state.h"
#include "../x86_emit.h"

/* 64-bit GPR access for MIPS place fns: each register is two dwords (low at
   base, high at base+4). ESI = MipsCpuState*; EAX/ECX/EDX scratch. */

namespace mips_emit {

inline int32_t GprLoOff(uint32_t r) {
    return static_cast<int32_t>(offsetof(MipsCpuState, gpr)) +
           static_cast<int32_t>(r) * 8;
}
inline int32_t GprHiOff(uint32_t r) { return GprLoOff(r) + 4; }

/* Load the low 32 bits of gpr[r] into a host register. */
inline void EmitLoadGprLo(uint8_t*& c, uint8_t reg, uint32_t r) {
    x86::EmitMovRegBaseDisp32(c, reg, x86::kStateReg, GprLoOff(r));
}

/* Store the 32-bit result in EAX into gpr[r], sign-extended to 64 bits.
   CDQ (Intel SDM Vol. 2: opcode 0x99) sign-extends EAX into EDX:EAX; EDX is
   clobbered (scratch). */
inline void EmitStoreGprSextEax(uint8_t*& c, uint32_t r) {
    x86::Emit8(c, 0x99);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(r), x86::kEax);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprHiOff(r), x86::kEdx);
}

/* Store a compile-time 64-bit immediate into gpr[r]. */
inline void EmitStoreGprImm64(uint8_t*& c, uint32_t r, uint32_t lo, uint32_t hi) {
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, GprLoOff(r), lo);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, GprHiOff(r), hi);
}

/* Store a 32-bit value sign-extended to 64 bits into gpr[r] (link registers,
   addresses, LUI). The high word is all-ones iff bit 31 is set. */
inline void EmitStoreGprSextImm32(uint8_t*& c, uint32_t r, uint32_t v) {
    EmitStoreGprImm64(c, r, v, (v & 0x80000000u) ? 0xFFFFFFFFu : 0u);
}

/* rd = sext32(rt[31:0] shifted by sa), for the immediate 32-bit shifts. ext is
   the x86 0xC1 /ext field: SHL 4 (SLL), SHR 5 (SRL), SAR 7 (SRA). sa==0 emits no
   shift (canonical sext-word). No store when rd==0. */
inline void EmitShiftImm32Sext(uint8_t*& c, uint32_t rd, uint32_t rt, uint32_t sa,
                               uint8_t ext) {
    if (rd == 0) {
        return;
    }
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rt));
    if (sa != 0) {
        x86::Emit8(c, 0xC1);                   /* shift r/m32, imm8 */
        x86::EmitModRmReg(c, 3, x86::kEax, ext);
        x86::Emit8(c, static_cast<uint8_t>(sa));
    }
    EmitStoreGprSextEax(c, rd);
}

/* rd = rs <op> rt, full 64-bit (both halves), for R-type bitwise ops. alu_opcode
   is the x86 "OP r32, r/m32" primary opcode byte: OR 0x0B, AND 0x23, XOR 0x33.
   No store when rd==0 (r0 is hardwired). */
inline void EmitRtypeBitwise64(uint8_t*& c, uint32_t rd, uint32_t rs, uint32_t rt,
                               uint8_t alu_opcode) {
    if (rd == 0) {
        return;
    }
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::Emit8(c, alu_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);   /* OP eax, [esi+rt.lo] */
    x86::Emit32(c, static_cast<uint32_t>(GprLoOff(rt)));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(rd), x86::kEax);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::Emit8(c, alu_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);   /* OP eax, [esi+rt.hi] */
    x86::Emit32(c, static_cast<uint32_t>(GprHiOff(rt)));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprHiOff(rd), x86::kEax);
}

/* rd = sext64(rs[31:0] <op> rt[31:0]) for 32-bit ADDU/SUBU. alu_opcode is the
   x86 "OP r32, r/m32" primary opcode (ADD 0x03, SUB 0x2B); the 32-bit result is
   sign-extended into the full register. No store when rd==0. */
inline void EmitRtypeArith32Sext(uint8_t*& c, uint32_t rd, uint32_t rs,
                                 uint32_t rt, uint8_t alu_opcode) {
    if (rd == 0) {
        return;
    }
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::Emit8(c, alu_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);   /* OP eax, [esi+rt.lo] */
    x86::Emit32(c, static_cast<uint32_t>(GprLoOff(rt)));
    EmitStoreGprSextEax(c, rd);
}

/* rd = rs <op> rt, full 64-bit with carry/borrow across the halves, for R-type
   DADDU/DSUBU. lo_opcode = low-half "OP r32,r/m32" primary (ADD 0x03 / SUB
   0x2B); hi_opcode = the carry-aware high-half form (ADC 0x13 / SBB 0x1B). No
   flag-clobbering op may sit between the halves. No store when rd==0. */
inline void EmitRtypeArith64(uint8_t*& c, uint32_t rd, uint32_t rs, uint32_t rt,
                             uint8_t lo_opcode, uint8_t hi_opcode) {
    if (rd == 0) {
        return;
    }
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::Emit8(c, lo_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);   /* OP eax, [esi+rt.lo] */
    x86::Emit32(c, static_cast<uint32_t>(GprLoOff(rt)));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(rd), x86::kEax);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::Emit8(c, hi_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);   /* OP eax, [esi+rt.hi] */
    x86::Emit32(c, static_cast<uint32_t>(GprHiOff(rt)));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprHiOff(rd), x86::kEax);
}

/* Equality-conditional branch: pc = (gpr[rs]==gpr[rt])==take_if_equal ? btgt
   : fall. Full 64-bit compare (both halves). Shared by BEQ (take_if_equal=true)
   and BNE (false); the delay slot is emitted separately by the block. */
inline void EmitEqBranch64(uint8_t*& c, uint32_t rs, uint32_t rt,
                           uint32_t btgt, uint32_t fall, int32_t pc_off,
                           bool take_if_equal) {
    const uint32_t pc_equal = take_if_equal ? btgt : fall;
    const uint32_t pc_neq   = take_if_equal ? fall : btgt;
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::EmitCmpRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rt));
    uint8_t* j_neq_lo = x86::EmitJnzLabel(c);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitCmpRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rt));
    uint8_t* j_neq_hi = x86::EmitJnzLabel(c);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, pc_off, pc_equal);
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* neq_label = c;
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, pc_off, pc_neq);
    uint8_t* done_label = c;
    x86::FixupLabel(j_neq_lo, neq_label);
    x86::FixupLabel(j_neq_hi, neq_label);
    x86::FixupLabel(j_done, done_label);
}

/* rd = (gpr[rs] < sext64(imm16)) ? 1 : 0, 64-bit compare via SUB low / SBB high
   then a SETcc, result 0/1 (hi=0). setcc_op is the x86 SETcc second opcode byte:
   SETB 0x92 (unsigned <, SLTIU) or SETL 0x9C (signed <, SLTI). No flag-clobbering
   op may sit between the subtract and the SETcc. No store when rd==0. */
inline void EmitSltImm64(uint8_t*& c, uint32_t rd, uint32_t rs, uint32_t imm16,
                         uint8_t setcc_op) {
    if (rd == 0) {
        return;
    }
    const uint32_t imm_lo = static_cast<uint32_t>(static_cast<int32_t>(
                                static_cast<int16_t>(imm16)));
    const uint8_t imm_hi8 = (imm16 & 0x8000u) ? 0xFFu : 0x00u;
    x86::EmitXorRegReg(c, x86::kEcx, x86::kEcx);                          /* ECX = 0 */
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::EmitSubRegImm32(c, x86::kEax, imm_lo);            /* CF = low borrow */
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));  /* MOV keeps CF */
    x86::Emit8(c, 0x83);                                   /* SBB eax, imm8 (83 /3 ib) */
    x86::EmitModRmReg(c, 3, x86::kEax, 3);
    x86::Emit8(c, imm_hi8);
    x86::Emit8(c, 0x0F);                                   /* SETcc cl */
    x86::Emit8(c, setcc_op);
    x86::EmitModRmReg(c, 3, x86::kCl, 0);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(rd), x86::kEcx);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, GprHiOff(rd), 0);
}

/* Sign-conditional branch on the 64-bit sign of gpr[rs] (= bit 31 of rs.hi):
   BLTZ (take_if_negative=true) takes when negative, BGEZ (false) when
   non-negative. pc_off is the cpu_state.pc offset; the delay slot is emitted
   separately by the block. */
inline void EmitSignBranch64(uint8_t*& c, uint32_t rs, uint32_t btgt, uint32_t fall,
                             int32_t pc_off, bool take_if_negative) {
    const uint32_t pc_if_neg    = take_if_negative ? btgt : fall;
    const uint32_t pc_if_nonneg = take_if_negative ? fall : btgt;
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_neg = x86::EmitJsLabel32(c);          /* SF=1 -> rs.hi<0 -> value<0 */
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, pc_off, pc_if_nonneg);
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* neg_label = c;
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, pc_off, pc_if_neg);
    uint8_t* done_label = c;
    x86::FixupLabel32(j_neg, neg_label);
    x86::FixupLabel(j_done, done_label);
}

/* Tail of a trapping 32-bit add (ADD / ADDI): EAX holds the result with OF set;
   on signed overflow call overflow_helper, else sign-extend EAX into gpr[dst]
   (no store when dst==0). */
inline void EmitTrappingArith32Tail(uint8_t*& c, uint32_t dst, void* jit,
                                    void* overflow_helper, uint32_t guest_addr) {
    uint8_t* j_ok = x86::EmitJnoLabel(c);
    x86::EmitPush32(c, guest_addr);
    x86::EmitPush32(c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(jit)));
    x86::EmitCall(c, overflow_helper);
    x86::FixupLabel(j_ok, c);
    if (dst != 0) {
        EmitStoreGprSextEax(c, dst);
    }
}

}  // namespace mips_emit
