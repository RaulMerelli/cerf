#pragma once

#include "x86_emit.h"

namespace x86 {

/* MMX register operand encodings: MM0..MM7 share slots 0..7 in the
   ModR/M reg field, like GPRs. */
constexpr uint8_t kMm0 = 0;

/* MOVQ MMx, [base + disp32] - 0F 6F /r mod=10. */
inline void EmitMovqMmRegBaseDisp32(uint8_t*& p, uint8_t mm, uint8_t base, int32_t disp) {
    Emit8(p, 0x0F); Emit8(p, 0x6F); EmitModRmReg(p, 2, base, mm);
    Emit32(p, static_cast<uint32_t>(disp));
}

/* MOVQ [base + disp32], MMx - 0F 7F /r mod=10. */
inline void EmitMovqBaseDisp32MmReg(uint8_t*& p, uint8_t base, int32_t disp, uint8_t mm) {
    Emit8(p, 0x0F); Emit8(p, 0x7F); EmitModRmReg(p, 2, base, mm);
    Emit32(p, static_cast<uint32_t>(disp));
}

/* MOVQ MMx, [base] - 0F 6F /r mod=00 (no displacement). */
inline void EmitMovqMmRegRegPtr(uint8_t*& p, uint8_t mm, uint8_t base) {
    Emit8(p, 0x0F); Emit8(p, 0x6F); EmitModRmReg(p, 0, base, mm);
}

/* MOVQ [base], MMx - 0F 7F /r mod=00. */
inline void EmitMovqRegPtrMmReg(uint8_t*& p, uint8_t base, uint8_t mm) {
    Emit8(p, 0x0F); Emit8(p, 0x7F); EmitModRmReg(p, 0, base, mm);
}

/* EMMS - empty MMX state; restores the x87 FPU tag word so any
   subsequent FPU op sees an empty stack. */
inline void EmitEmms(uint8_t*& p) {
    Emit8(p, 0x0F); Emit8(p, 0x77);
}

}  // namespace x86
