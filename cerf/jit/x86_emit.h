#pragma once

/* Encodings: Intel SDM Vol. 2; ModR/M forms per Vol. 2A Table 2-2 (p. 2-6). */

#include <cstdint>
#include <cstring>

#include "../core/log.h"

namespace x86 {

/* r32 / r8 field encodings - SDM Vol. 2A Table 2-2. */
constexpr uint8_t kEax = 0;
constexpr uint8_t kEcx = 1;
constexpr uint8_t kEdx = 2;
constexpr uint8_t kEbx = 3;
constexpr uint8_t kEsp = 4;
constexpr uint8_t kEbp = 5;
constexpr uint8_t kEsi = 6;

constexpr uint8_t kMmuReg   = kEbx;
constexpr uint8_t kStateReg = kEsi;

constexpr uint8_t kAl = 0;
constexpr uint8_t kCl = 1;
constexpr uint8_t kDl = 2;

inline void Emit8(uint8_t*& c, uint8_t v) {
    *c++ = v;
}

inline void Emit32(uint8_t*& c, uint32_t v) {
    std::memcpy(c, &v, 4);
    c += 4;
}

/* ModR/M byte = (mod << 6) | (reg << 3) | r/m - SDM Vol. 2A Table 2-2. */
inline void EmitModRmByte(uint8_t*& c, uint8_t mod, uint8_t rm, uint8_t reg) {
    Emit8(c, static_cast<uint8_t>((mod << 6) | (reg << 3) | rm));
}

/* Per Table 2-2 notes 1-2: in the memory forms, r/m = 100b means a SIB
   byte follows, and mod = 00b with r/m = 101b means bare disp32. */
inline void EmitModRmReg(uint8_t*& c, uint8_t mod, uint8_t rm, uint8_t reg) {
    if (mod != 3u && rm == kEsp) {
        LOG(Caution, "x86 emit: r/m=100b in a memory form requires a SIB "
                "byte (mod=%u)\n", mod);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (mod == 0u && rm == kEbp) {
        LOG(Caution, "x86 emit: mod=00b r/m=101b encodes disp32, not "
                "[EBP]\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    EmitModRmByte(c, mod, rm, reg);
}

/* mod = 00b, r/m = 101b - bare disp32 (SDM Vol. 2A Table 2-2 note 2). */
inline void EmitModRmDisp32(uint8_t*& c, uint8_t reg) {
    EmitModRmByte(c, 0, kEbp, reg);
}

/* rel32 - displacement relative to the next instruction:
   SDM Vol. 2A 3-139 CALL, 3-552 JMP. */
inline void EmitOpcodeRel32(uint8_t*& c, uint8_t opcode, const void* target) {
    Emit8(c, opcode);
    Emit32(c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) -
                                    reinterpret_cast<uintptr_t>(c + 4)));
}

/* CALL rel32 - E8 cd (SDM Vol. 2A 3-139 CALL). */
inline void EmitCall(uint8_t*& c, const void* target) {
    EmitOpcodeRel32(c, 0xE8, target);
}

/* JMP rel32 - E9 cd (SDM Vol. 2A 3-552 JMP). */
inline void EmitJmp32(uint8_t*& c, const void* target) {
    EmitOpcodeRel32(c, 0xE9, target);
}

/* RET - C3, Op/En ZO (SDM Vol. 2B 4-564 RET). */
inline void EmitRet(uint8_t*& c) {
    Emit8(c, 0xC3);
}

inline uint8_t* EmitRel8Label(uint8_t*& c, uint8_t opcode) {
    Emit8(c, opcode);
    uint8_t* label = c;
    Emit8(c, 0);
    return label;
}

inline uint8_t* EmitRel32Label(uint8_t*& c, uint8_t opcode0F) {
    Emit8(c, 0x0F);
    Emit8(c, opcode0F);
    uint8_t* label = c;
    Emit32(c, 0);
    return label;
}

/* JMP rel8 - EB cb (SDM Vol. 2A 3-552 JMP). */
inline uint8_t* EmitJmpLabel(uint8_t*& c) { return EmitRel8Label(c, 0xEB); }

/* JMP rel32 - E9 cd (SDM Vol. 2A 3-552 JMP). */
inline uint8_t* EmitJmpLabel32(uint8_t*& c) {
    Emit8(c, 0xE9);
    uint8_t* label = c;
    Emit32(c, 0);
    return label;
}

/* Jcc rel8 - 7x cb; Jcc rel32 - 0F 8x cd (SDM Vol. 2A 3-547..3-549 Jcc). */
inline uint8_t* EmitJzLabel(uint8_t*& c)    { return EmitRel8Label(c, 0x74); }
inline uint8_t* EmitJnzLabel(uint8_t*& c)   { return EmitRel8Label(c, 0x75); }
inline uint8_t* EmitJnoLabel(uint8_t*& c)   { return EmitRel8Label(c, 0x71); }
inline uint8_t* EmitJzLabel32(uint8_t*& c)  { return EmitRel32Label(c, 0x84); }
inline uint8_t* EmitJnzLabel32(uint8_t*& c) { return EmitRel32Label(c, 0x85); }
inline uint8_t* EmitJsLabel32(uint8_t*& c)  { return EmitRel32Label(c, 0x88); }
inline uint8_t* EmitJaeLabel32(uint8_t*& c) { return EmitRel32Label(c, 0x83); }
inline uint8_t* EmitJncLabel32(uint8_t*& c) { return EmitRel32Label(c, 0x83); }
inline uint8_t* EmitJbLabel32(uint8_t*& c)  { return EmitRel32Label(c, 0x82); }

/* rel8 - sign-extended 8-bit displacement: SDM Vol. 2A 3-552 JMP (EB cb),
   3-547 Jcc (7x cb). */
inline void FixupLabel(uint8_t* label, uint8_t* cursor) {
    const ptrdiff_t disp = cursor - (label + 1);
    if (disp < -128 || disp > 127) {
        LOG(Caution, "x86 emit: rel8 fixup out of range - opcode 0x%02X, "
                "displacement %d\n", label[-1], static_cast<int>(disp));
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    label[0] = static_cast<uint8_t>(disp);
}

inline void FixupLabel32(uint8_t* label, uint8_t* cursor) {
    const uint32_t disp = static_cast<uint32_t>(cursor - (label + 4));
    std::memcpy(label, &disp, 4);
}

/* MOV r32, imm32 - B8+rd id (SDM Vol. 2B 4-35 MOV). */
inline void EmitMovRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    Emit8(c, static_cast<uint8_t>(0xB8 + reg));
    Emit32(c, imm);
}

/* MOV r32, r/m32 - 8B /r, register-direct (SDM Vol. 2B 4-35 MOV). */
inline void EmitMovRegReg(uint8_t*& c, uint8_t dst, uint8_t src) {
    Emit8(c, 0x8B);
    EmitModRmReg(c, 3, src, dst);
}

/* MOV r32, [base + disp32] - 8B /r mod=10 (SDM Vol. 2B 4-35 MOV). */
inline void EmitMovRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    Emit8(c, 0x8B);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* MOV [base + disp32], r32 - 89 /r mod=10 (SDM Vol. 2B 4-35 MOV). */
inline void EmitMovBaseDisp32Reg(uint8_t*& c, uint8_t base, int32_t disp,
                                 uint8_t reg) {
    Emit8(c, 0x89);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* MOV r/m32, imm32 - C7 /0 id, mod=10 (SDM Vol. 2B 4-35 MOV). */
inline void EmitMovBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp,
                                   uint32_t imm) {
    Emit8(c, 0xC7);
    EmitModRmReg(c, 2, base, 0);
    Emit32(c, static_cast<uint32_t>(disp));
    Emit32(c, imm);
}

/* MOV r/m8, r8 - 88 /r mod=10 (SDM Vol. 2B 4-35 MOV). */
inline void EmitMovBaseDisp32Byte(uint8_t*& c, uint8_t base, int32_t disp,
                                  uint8_t reg8) {
    Emit8(c, 0x88);
    EmitModRmReg(c, 2, base, reg8);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* MOV [disp32], r32 - 89 /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
inline void EmitMovDwordPtrReg(uint8_t*& c, const void* addr, uint8_t reg) {
    Emit8(c, 0x89);
    EmitModRmDisp32(c, reg);
    Emit32(c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
}

/* MOVSX r32, r/m8 - 0F BE /r mod=10 (SDM Vol. 2B 4-130 MOVSX). */
inline void EmitMovsxByteRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                       int32_t disp) {
    Emit8(c, 0x0F);
    Emit8(c, 0xBE);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* MOVSX r32, r/m8 - 0F BE /r, register-direct (SDM Vol. 2B 4-130 MOVSX). */
inline void EmitMovsxReg32Reg8(uint8_t*& c, uint8_t dst, uint8_t src8) {
    Emit8(c, 0x0F);
    Emit8(c, 0xBE);
    EmitModRmReg(c, 3, src8, dst);
}

/* MOVSX r32, r/m16 - 0F BF /r, register-direct (SDM Vol. 2B 4-130 MOVSX). */
inline void EmitMovsxReg32Reg16(uint8_t*& c, uint8_t dst, uint8_t src16) {
    Emit8(c, 0x0F);
    Emit8(c, 0xBF);
    EmitModRmReg(c, 3, src16, dst);
}

/* MOVZX r32, r/m8 - 0F B6 /r, register-direct (SDM Vol. 2B 4-140 MOVZX). */
inline void EmitMovzxReg32Reg8(uint8_t*& c, uint8_t dst, uint8_t src8) {
    Emit8(c, 0x0F);
    Emit8(c, 0xB6);
    EmitModRmReg(c, 3, src8, dst);
}

/* MOVZX r32, r/m16 - 0F B7 /r, register-direct (SDM Vol. 2B 4-140 MOVZX). */
inline void EmitMovzxReg32Reg16(uint8_t*& c, uint8_t dst, uint8_t src16) {
    Emit8(c, 0x0F);
    Emit8(c, 0xB7);
    EmitModRmReg(c, 3, src16, dst);
}

/* LEA r32, m - 8D /r mod=10 (SDM Vol. 2A 3-594 LEA). */
inline void EmitLeaRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    Emit8(c, 0x8D);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* ADD r/m32, imm32 - 81 /0 id, register-direct (SDM Vol. 2A 3-32 ADD). */
inline void EmitAddRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    Emit8(c, 0x81);
    EmitModRmReg(c, 3, reg, 0);
    Emit32(c, imm);
}

/* ADD r32, r/m32 - 03 /r, register-direct (SDM Vol. 2A 3-32 ADD). */
inline void EmitAddReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    Emit8(c, 0x03);
    EmitModRmReg(c, 3, src, dst);
}

/* ADD r32, [base + disp32] - 03 /r mod=10 (SDM Vol. 2A 3-32 ADD). */
inline void EmitAddRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    Emit8(c, 0x03);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* ADD r/m32, imm8 (sign-extended) - 83 /0 ib mod=10
   (SDM Vol. 2A 3-32 ADD). */
inline void EmitAddBaseDisp32Imm8(uint8_t*& c, uint8_t base, int32_t disp,
                                  uint8_t imm8) {
    Emit8(c, 0x83);
    EmitModRmReg(c, 2, base, 0);
    Emit32(c, static_cast<uint32_t>(disp));
    Emit8(c, imm8);
}

/* ADC r/m32, imm8 (sign-extended) - 83 /2 ib mod=10
   (SDM Vol. 2A 3-27 ADC). */
inline void EmitAdcBaseDisp32Imm8(uint8_t*& c, uint8_t base, int32_t disp,
                                  uint8_t imm8) {
    Emit8(c, 0x83);
    EmitModRmReg(c, 2, base, 2);
    Emit32(c, static_cast<uint32_t>(disp));
    Emit8(c, imm8);
}

/* SUB r32, r/m32 - 2B /r, register-direct (SDM Vol. 2B 4-681 SUB). */
inline void EmitSubReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    Emit8(c, 0x2B);
    EmitModRmReg(c, 3, src, dst);
}

/* SUB r32, [base + disp32] - 2B /r mod=10 (SDM Vol. 2B 4-681 SUB). */
inline void EmitSubRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    Emit8(c, 0x2B);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* SBB r32, [base + disp32] - 1B /r mod=10 (SDM Vol. 2B 4-608 SBB). */
inline void EmitSbbRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    Emit8(c, 0x1B);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* AND r/m32, imm32 - 81 /4 id, register-direct (SDM Vol. 2A 3-78 AND). */
inline void EmitAndRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    Emit8(c, 0x81);
    EmitModRmReg(c, 3, reg, 4);
    Emit32(c, imm);
}

/* AND r/m32, imm32 - 81 /4 id, mod=10 (SDM Vol. 2A 3-78 AND). */
inline void EmitAndBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp,
                                   uint32_t imm) {
    Emit8(c, 0x81);
    EmitModRmReg(c, 2, base, 4);
    Emit32(c, static_cast<uint32_t>(disp));
    Emit32(c, imm);
}

/* OR r32, r/m32 - 0B /r, register-direct (SDM Vol. 2B 4-172 OR). */
inline void EmitOrReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    Emit8(c, 0x0B);
    EmitModRmReg(c, 3, src, dst);
}

/* OR r32, [base + disp32] - 0B /r mod=10 (SDM Vol. 2B 4-172 OR). */
inline void EmitOrRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                int32_t disp) {
    Emit8(c, 0x0B);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* OR r/m32, imm32 - 81 /1 id, mod=10 (SDM Vol. 2B 4-172 OR). */
inline void EmitOrBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp,
                                  uint32_t imm) {
    Emit8(c, 0x81);
    EmitModRmReg(c, 2, base, 1);
    Emit32(c, static_cast<uint32_t>(disp));
    Emit32(c, imm);
}

/* XOR r32, r/m32 - 33 /r, register-direct (SDM Vol. 2D 6-36 XOR). */
inline void EmitXorRegReg(uint8_t*& c, uint8_t dst, uint8_t src) {
    Emit8(c, 0x33);
    EmitModRmReg(c, 3, src, dst);
}

/* CMP r/m32, imm32 - 81 /7 id, register-direct (SDM Vol. 2A 3-179 CMP). */
inline void EmitCmpRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    Emit8(c, 0x81);
    EmitModRmReg(c, 3, reg, 7);
    Emit32(c, imm);
}

/* CMP r32, r/m32 - 3B /r mod=10 (SDM Vol. 2A 3-179 CMP). */
inline void EmitCmpRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    Emit8(c, 0x3B);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* CMP r8, r/m8 - 3A /r mod=10 (SDM Vol. 2A 3-179 CMP). */
inline void EmitCmpReg8BaseDisp32(uint8_t*& c, uint8_t reg8, uint8_t base,
                                  int32_t disp) {
    Emit8(c, 0x3A);
    EmitModRmReg(c, 2, base, reg8);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* TEST r/m32, r32 - 85 /r, register-direct (SDM Vol. 2B 4-711 TEST). */
inline void EmitTestRegReg(uint8_t*& c, uint8_t a, uint8_t b) {
    Emit8(c, 0x85);
    EmitModRmReg(c, 3, a, b);
}

/* TEST r/m32, imm32 - F7 /0 id, register-direct (SDM Vol. 2B 4-711 TEST). */
inline void EmitTestRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    Emit8(c, 0xF7);
    EmitModRmReg(c, 3, reg, 0);
    Emit32(c, imm);
}

/* TEST r/m8, imm8 - F6 /0 ib mod=10 (SDM Vol. 2B 4-711 TEST). */
inline void EmitTestByteBaseDisp32Imm8(uint8_t*& c, uint8_t base, int32_t disp,
                                       uint8_t imm8) {
    Emit8(c, 0xF6);
    EmitModRmReg(c, 2, base, 0);
    Emit32(c, static_cast<uint32_t>(disp));
    Emit8(c, imm8);
}

/* BT r/m32, r32 - 0F A3 /r, mod=00 [base] (SDM Vol. 2A 3-130 BT). */
inline void EmitBtMemReg(uint8_t*& c, uint8_t base, uint8_t reg) {
    Emit8(c, 0x0F);
    Emit8(c, 0xA3);
    EmitModRmReg(c, 0, base, reg);
}

/* BTS r/m32, r32 - 0F AB /r, mod=00 [base] (SDM Vol. 2A 3-136 BTS). */
inline void EmitBtsMemReg(uint8_t*& c, uint8_t base, uint8_t reg) {
    Emit8(c, 0x0F);
    Emit8(c, 0xAB);
    EmitModRmReg(c, 0, base, reg);
}

/* SETC r/m8 - 0F 92, register-direct; the reg field is unused by SETcc
   (SDM Vol. 2B 4-618 SETcc, operand encoding M). */
inline void EmitSetcReg8(uint8_t*& c, uint8_t reg8) {
    Emit8(c, 0x0F);
    Emit8(c, 0x92);
    EmitModRmReg(c, 3, reg8, 0);
}

/* SHL r/m32, imm8 - C1 /4 ib, register-direct (SDM Vol. 2B 4-600 SHL). */
inline void EmitShlReg32Imm(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    Emit8(c, 0xC1);
    EmitModRmReg(c, 3, reg, 4);
    Emit8(c, imm8);
}

/* SHR r/m32, imm8 - C1 /5 ib, register-direct (SDM Vol. 2B 4-600 SHR). */
inline void EmitShrReg32Imm(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    Emit8(c, 0xC1);
    EmitModRmReg(c, 3, reg, 5);
    Emit8(c, imm8);
}

/* SAR r/m32, imm8 - C1 /7 ib, register-direct (SDM Vol. 2B 4-599 SAR). */
inline void EmitSarReg32Imm(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    Emit8(c, 0xC1);
    EmitModRmReg(c, 3, reg, 7);
    Emit8(c, imm8);
}

/* ROR r/m32, imm8 - C1 /1 ib, register-direct
   (SDM Vol. 2B 4-533 RCL/RCR/ROL/ROR). */
inline void EmitRorReg32Imm(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    Emit8(c, 0xC1);
    EmitModRmReg(c, 3, reg, 1);
    Emit8(c, imm8);
}

/* ROR r/m32, CL - D3 /1, register-direct
   (SDM Vol. 2B 4-533 RCL/RCR/ROL/ROR). */
inline void EmitRorReg32Cl(uint8_t*& c, uint8_t reg) {
    Emit8(c, 0xD3);
    EmitModRmReg(c, 3, reg, 1);
}

/* BSWAP r32 - 0F C8+rd (SDM Vol. 2A 3-129 BSWAP). */
inline void EmitBswapReg32(uint8_t*& c, uint8_t reg) {
    Emit8(c, 0x0F);
    Emit8(c, static_cast<uint8_t>(0xC8 + reg));
}

/* NOT r/m32 - F7 /2, mod=10 (SDM Vol. 2B 4-170 NOT). */
inline void EmitNotBaseDisp32(uint8_t*& c, uint8_t base, int32_t disp) {
    Emit8(c, 0xF7);
    EmitModRmReg(c, 2, base, 2);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* CDQ - 99, EDX:EAX = sign-extend of EAX (SDM Vol. 2A 3-314 CWD/CDQ/CQO). */
inline void EmitCdq(uint8_t*& c) {
    Emit8(c, 0x99);
}

/* PUSH imm32 - 68 id (SDM Vol. 2B 4-521 PUSH). */
inline void EmitPush32(uint8_t*& c, uint32_t imm) {
    Emit8(c, 0x68);
    Emit32(c, imm);
}

/* PUSH r32 - 50+rd (SDM Vol. 2B 4-521 PUSH). */
inline void EmitPushReg(uint8_t*& c, uint8_t reg) {
    Emit8(c, static_cast<uint8_t>(0x50 + reg));
}

/* PUSH r/m32 - FF /6 mod=10 (SDM Vol. 2B 4-521 PUSH). */
inline void EmitPushBaseDisp32(uint8_t*& c, uint8_t base, int32_t disp) {
    Emit8(c, 0xFF);
    EmitModRmReg(c, 2, base, 6);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* POP r32 - 58+rd (SDM Vol. 2B 4-398 POP). */
inline void EmitPopReg(uint8_t*& c, uint8_t reg) {
    Emit8(c, static_cast<uint8_t>(0x58 + reg));
}

}  // namespace x86
