#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* OR rd, rs, rt : rd = rs | rt, full 64-bit (both dwords). */
uint8_t* PlaceMipsOr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    mips_emit::EmitLoadGprLo(cursor, kEax, d->rs);
    EmitOrRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rd), kEax);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs));
    EmitOrRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rt));
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprHiOff(d->rd), kEax);
    return cursor;
}
