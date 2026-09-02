#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

}  /* namespace */

uint8_t* PlaceRev(uint8_t*      cursor,
                  DecodedInsn*  d,
                  BlockContext* /*ctx*/) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    EmitBswapReg32      (cursor, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}

uint8_t* PlaceRev16(uint8_t*      cursor,
                    DecodedInsn*  d,
                    BlockContext* /*ctx*/) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    EmitBswapReg32      (cursor, kEax);
    EmitRorReg32Imm     (cursor, kEax, 16);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}

uint8_t* PlaceRevsh(uint8_t*      cursor,
                    DecodedInsn*  d,
                    BlockContext* /*ctx*/) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    EmitBswapReg32      (cursor, kEax);
    EmitSarReg32Imm     (cursor, kEax, 16);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}

uint8_t* PlaceRbit(uint8_t* cursor, DecodedInsn* d, BlockContext* /*ctx*/) {
    using namespace x86;
    /* DDI 0406C.d A8.8.145 RBIT p. A8-561: reverse all 32 bits. Three mask/shift
       swap rounds (adjacent bits, bit pairs, nibbles) followed by a
       byte-order swap complete the reversal. */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rm));
    struct SwapRound {
        uint32_t mask;
        uint8_t shift;
    };
    static constexpr SwapRound kRounds[3] = {
        {0x55555555u, 1u},
        {0x33333333u, 2u},
        {0x0F0F0F0Fu, 4u},
    };
    for (const SwapRound& r : kRounds) {
        EmitMovRegReg(cursor, kEdx, kEax);
        EmitShrReg32Imm(cursor, kEdx, r.shift);
        EmitAndRegImm32(cursor, kEax, r.mask);
        EmitAndRegImm32(cursor, kEdx, r.mask);
        EmitShlReg32Imm(cursor, kEax, r.shift);
        EmitOrReg32Reg32(cursor, kEax, kEdx);
    }
    EmitBswapReg32(cursor, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rd), kEax);
    return cursor;
}
