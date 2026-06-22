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

}  // namespace mips_emit
