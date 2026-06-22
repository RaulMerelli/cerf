#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* ORI rt, rs, imm : rt = rs | zero_extend(imm16), full 64-bit. The immediate
   only touches the low bits; rs's high word passes through, so it is copied
   even when rt != rs (skipping the copy would leave rt's stale high word). */
uint8_t* PlaceMipsOri(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rt == 0) {
        return cursor;
    }
    mips_emit::EmitLoadGprLo(cursor, kEax, d->rs);
    EmitMovRegImm32(cursor, kEcx, d->imm);   /* d->imm = word & 0xffff (zero-extended) */
    EmitOrReg32Reg32(cursor, kEax, kEcx);
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rt), kEax);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs));
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprHiOff(d->rt), kEax);
    return cursor;
}
