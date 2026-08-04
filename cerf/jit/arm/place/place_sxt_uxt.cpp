#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

/* DDI 0406C A8.8.233/.235/.274/.276 A1: rotation = UInt(rotate:'000'),
   a ROR by 0/8/16/24 before extraction. */
inline void EmitLoadAndRotate(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    const uint32_t rotate_bits = d->op1 * 8u;
    if (rotate_bits != 0u) {
        EmitRorReg32Imm(cursor, kEax, static_cast<uint8_t>(rotate_bits));
    }
}

inline void EmitStoreRd(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
}

}  /* namespace */

uint8_t* PlaceSxtb(uint8_t* cursor, DecodedInsn* d, BlockContext* /*ctx*/) {
    using namespace x86;
    EmitLoadAndRotate (cursor, d);
    EmitMovsxReg32Reg8(cursor, kEax, kAl);
    EmitStoreRd       (cursor, d);
    return cursor;
}

uint8_t* PlaceUxtb(uint8_t* cursor, DecodedInsn* d, BlockContext* /*ctx*/) {
    using namespace x86;
    EmitLoadAndRotate (cursor, d);
    EmitMovzxReg32Reg8(cursor, kEax, kAl);
    EmitStoreRd       (cursor, d);
    return cursor;
}

uint8_t* PlaceSxth(uint8_t* cursor, DecodedInsn* d, BlockContext* /*ctx*/) {
    using namespace x86;
    EmitLoadAndRotate  (cursor, d);
    EmitMovsxReg32Reg16(cursor, kEax, kEax);
    EmitStoreRd        (cursor, d);
    return cursor;
}

uint8_t* PlaceUxth(uint8_t* cursor, DecodedInsn* d, BlockContext* /*ctx*/) {
    using namespace x86;
    EmitLoadAndRotate  (cursor, d);
    EmitMovzxReg32Reg16(cursor, kEax, kEax);
    EmitStoreRd        (cursor, d);
    return cursor;
}
