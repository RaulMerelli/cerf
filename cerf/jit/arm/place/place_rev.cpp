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
