#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

/* x86 ROR r32, imm8 - opcode 0xC1 /1 ib. */
inline void EmitRorRegImm8(uint8_t*& p, uint8_t reg, uint8_t count) {
    x86::Emit8(p, 0xC1);
    x86::EmitModRmReg(p, /*mod=*/3, /*rm=*/reg, /*reg=*/1);
    x86::Emit8(p, count);
}

/* MOVSX EAX, AL - opcode 0F BE C0. Sign-extends the low byte of
   EAX to all 32 bits. */
inline void EmitMovsxEaxAl(uint8_t*& p) {
    x86::Emit8(p, 0x0F);
    x86::Emit8(p, 0xBE);
    x86::Emit8(p, 0xC0);
}

/* MOVZX EAX, AL - opcode 0F B6 C0. Zero-extends the low byte. */
inline void EmitMovzxEaxAl(uint8_t*& p) {
    x86::Emit8(p, 0x0F);
    x86::Emit8(p, 0xB6);
    x86::Emit8(p, 0xC0);
}

/* MOVSX EAX, AX - opcode 0F BF C0. Sign-extends the low 16 bits. */
inline void EmitMovsxEaxAx(uint8_t*& p) {
    x86::Emit8(p, 0x0F);
    x86::Emit8(p, 0xBF);
    x86::Emit8(p, 0xC0);
}

/* MOVZX EAX, AX - opcode 0F B7 C0. Zero-extends the low 16 bits. */
inline void EmitMovzxEaxAx(uint8_t*& p) {
    x86::Emit8(p, 0x0F);
    x86::Emit8(p, 0xB7);
    x86::Emit8(p, 0xC0);
}

/* Common prefix: load Rm into EAX, apply ROR by rot*8 if non-zero. */
inline void EmitLoadAndRotate(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    const uint32_t rotate_bits = d->op1 * 8u;
    if (rotate_bits != 0u) {
        EmitRorRegImm8(cursor, kEax, static_cast<uint8_t>(rotate_bits));
    }
}

inline void EmitStoreRd(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
}

}  /* namespace */

uint8_t* PlaceSxtb(uint8_t* cursor, DecodedInsn* d, BlockContext* /*ctx*/) {
    EmitLoadAndRotate(cursor, d);
    EmitMovsxEaxAl   (cursor);
    EmitStoreRd      (cursor, d);
    return cursor;
}

uint8_t* PlaceUxtb(uint8_t* cursor, DecodedInsn* d, BlockContext* /*ctx*/) {
    EmitLoadAndRotate(cursor, d);
    EmitMovzxEaxAl   (cursor);
    EmitStoreRd      (cursor, d);
    return cursor;
}

uint8_t* PlaceSxth(uint8_t* cursor, DecodedInsn* d, BlockContext* /*ctx*/) {
    EmitLoadAndRotate(cursor, d);
    EmitMovsxEaxAx   (cursor);
    EmitStoreRd      (cursor, d);
    return cursor;
}

uint8_t* PlaceUxth(uint8_t* cursor, DecodedInsn* d, BlockContext* /*ctx*/) {
    EmitLoadAndRotate(cursor, d);
    EmitMovzxEaxAx   (cursor);
    EmitStoreRd      (cursor, d);
    return cursor;
}
