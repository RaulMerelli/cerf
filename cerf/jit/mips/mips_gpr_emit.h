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

inline int32_t BranchStateOff() { return static_cast<int32_t>(offsetof(MipsCpuState, branch_state)); }
inline int32_t BtargetOff()     { return static_cast<int32_t>(offsetof(MipsCpuState, btarget)); }
inline int32_t BcondOff()       { return static_cast<int32_t>(offsetof(MipsCpuState, bcond)); }

/* j/jal: record an unconditional branch to a compile-time target (QEMU
   gen_compute_branch OPC_J/JAL -> MIPS_HFLAG_B). */
inline void EmitBranchUncond(uint8_t*& c, uint32_t btarget) {
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), MipsBranch::kUncond);
}

/* jr/jalr: btarget = gpr[rs] low 32, read NOW (the delay slot may clobber rs).
   QEMU gen_compute_branch OPC_JR/JALR gen_load_gpr(btarget, rs) -> MIPS_HFLAG_BR. */
inline void EmitBranchRegister(uint8_t*& c, uint32_t rs) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BtargetOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), MipsBranch::kRegister);
}

/* beq/bne: bcond = (gpr[rs]==gpr[rt]) 64-bit for take_if_equal, else negated,
   computed NOW (delay slot may clobber rs/rt). QEMU OPC_BEQ/BNE -> MIPS_HFLAG_BC. */
inline void EmitBranchCondEq(uint8_t*& c, uint32_t rs, uint32_t rt, uint32_t btarget,
                             bool take_if_equal) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::EmitCmpRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rt));
    uint8_t* j_neq_lo = x86::EmitJnzLabel(c);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitCmpRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rt));
    uint8_t* j_neq_hi = x86::EmitJnzLabel(c);
    x86::EmitMovRegImm32(c, x86::kEax, take_if_equal ? 1u : 0u);   /* equal */
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* neq_label = c;
    x86::FixupLabel(j_neq_lo, neq_label);
    x86::FixupLabel(j_neq_hi, neq_label);
    x86::EmitMovRegImm32(c, x86::kEax, take_if_equal ? 0u : 1u);   /* not equal */
    x86::FixupLabel(j_done, c);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BcondOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), MipsBranch::kCond);
}

/* bltz/bgez: bcond = sign of the 64-bit gpr[rs] (= hi bit31): take_if_neg for
   BLTZ, !take_if_neg for BGEZ. QEMU OPC_BLTZ/BGEZ setcondi LT/GE 0. */
inline void EmitBranchCondSign(uint8_t*& c, uint32_t rs, uint32_t btarget,
                               bool take_if_neg) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_neg = x86::EmitJsLabel32(c);
    x86::EmitMovRegImm32(c, x86::kEax, take_if_neg ? 0u : 1u);     /* >= 0 */
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* neg_label = c;
    x86::FixupLabel32(j_neg, neg_label);
    x86::EmitMovRegImm32(c, x86::kEax, take_if_neg ? 1u : 0u);     /* < 0 */
    x86::FixupLabel(j_done, c);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BcondOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), MipsBranch::kCond);
}

/* bgtz: bcond = (int64)gpr[rs] > 0 (hi>0, or hi==0 && lo!=0). A hi==0 value with
   lo bit31 set is +2^31 (>0), so the test must not collapse to a 32-bit signed
   lo>0. QEMU OPC_BGTZ setcondi GT 0. */
inline void EmitBranchCondGtz(uint8_t*& c, uint32_t rs, uint32_t btarget) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_neg = x86::EmitJsLabel32(c);     /* hi<0 -> value<0 -> not >0 */
    uint8_t* j_pos = x86::EmitJnzLabel(c);      /* hi>0 -> >0 */
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));  /* hi==0 */
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_lo_nz = x86::EmitJnzLabel(c);    /* lo!=0 -> >0 */
    uint8_t* nottaken_label = c;
    x86::FixupLabel32(j_neg, nottaken_label);
    x86::EmitMovRegImm32(c, x86::kEax, 0u);     /* hi==0 && lo==0 -> ==0 -> not >0 */
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* taken_label = c;
    x86::FixupLabel(j_pos, taken_label);
    x86::FixupLabel(j_lo_nz, taken_label);
    x86::EmitMovRegImm32(c, x86::kEax, 1u);
    x86::FixupLabel(j_done, c);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BcondOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), MipsBranch::kCond);
}

/* blez: bcond = (int64)gpr[rs] <= 0 (hi<0, or hi==0 && lo==0) - the negation of
   the bgtz >0 test. QEMU OPC_BLEZ setcondi LE 0. */
inline void EmitBranchCondLez(uint8_t*& c, uint32_t rs, uint32_t btarget) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_taken_neg = x86::EmitJsLabel32(c);   /* hi<0 -> value<0 -> <=0 */
    uint8_t* j_nt_hi = x86::EmitJnzLabel(c);        /* hi>0 -> >0 -> not <=0 */
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));  /* hi==0 */
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_nt_lo = x86::EmitJnzLabel(c);        /* lo!=0 -> >0 -> not <=0 */
    uint8_t* taken_label = c;                        /* hi<0 (js) or hi==0 && lo==0 */
    x86::FixupLabel32(j_taken_neg, taken_label);
    x86::EmitMovRegImm32(c, x86::kEax, 1u);
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* nt_label = c;
    x86::FixupLabel(j_nt_hi, nt_label);
    x86::FixupLabel(j_nt_lo, nt_label);
    x86::EmitMovRegImm32(c, x86::kEax, 0u);
    x86::FixupLabel(j_done, c);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BcondOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), MipsBranch::kCond);
}

}  // namespace mips_emit
