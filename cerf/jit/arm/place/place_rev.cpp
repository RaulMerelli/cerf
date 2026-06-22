#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

/* x86 BSWAP r32 - opcode 0x0F 0xC8+r. EAX = 0, so encoding is
   0x0F 0xC8. */
inline void EmitBswapEax(uint8_t*& p) {
    x86::Emit8(p, 0x0F);
    x86::Emit8(p, 0xC8);
}

/* x86 ROR r32, imm8 - opcode 0xC1 /1 ib. */
inline void EmitRorRegImm8(uint8_t*& p, uint8_t reg, uint8_t count) {
    x86::Emit8(p, 0xC1);
    x86::EmitModRmReg(p, /*mod=*/3, /*rm=*/reg, /*reg=*/1);
    x86::Emit8(p, count);
}

/* x86 SAR r32, imm8 - opcode 0xC1 /7 ib. */
inline void EmitSarRegImm8(uint8_t*& p, uint8_t reg, uint8_t count) {
    x86::Emit8(p, 0xC1);
    x86::EmitModRmReg(p, /*mod=*/3, /*rm=*/reg, /*reg=*/7);
    x86::Emit8(p, count);
}

}  /* namespace */

/* REV : Rd = byte-reverse(Rm) - single BSWAP. */
uint8_t* PlaceRev(uint8_t*      cursor,
                  DecodedInsn*  d,
                  BlockContext* /*ctx*/) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    EmitBswapEax(cursor);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}

/* REV16 : per-halfword byte swap. BSWAP gives full reverse
   (AABBCCDD → DDCCBBAA), ROR by 16 swaps halves back to BBAADDCC
   - each halfword's bytes swapped. */
uint8_t* PlaceRev16(uint8_t*      cursor,
                    DecodedInsn*  d,
                    BlockContext* /*ctx*/) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    EmitBswapEax        (cursor);
    EmitRorRegImm8      (cursor, kEax, 16);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}

uint8_t* PlaceRevsh(uint8_t*      cursor,
                    DecodedInsn*  d,
                    BlockContext* /*ctx*/) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    EmitBswapEax        (cursor);
    EmitSarRegImm8      (cursor, kEax, 16);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}
