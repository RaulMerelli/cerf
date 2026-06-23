#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* SLT rd, rs, rt : rd = (int64)rs < (int64)rt ? 1 : 0. SUB low / SBB high leaves
   SF/OF on the high SBB = the 64-bit signed compare; SETL (SF!=OF) captures it
   into pre-zeroed ECX, so no flag-clobbering op may sit between the SBB and the
   SETL. Result 0/1, hi word 0. */
uint8_t* PlaceMipsSlt(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    EmitXorRegReg(cursor, kEcx, kEcx);                                       /* ECX = 0 */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitSubRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt)); /* CF = low borrow */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs)); /* MOV keeps CF */
    Emit8(cursor, 0x1B);                                  /* SBB eax, [esi+rt.hi] */
    EmitModRmReg(cursor, 2, kStateReg, kEax);
    Emit32(cursor, static_cast<uint32_t>(mips_emit::GprHiOff(d->rt)));
    Emit8(cursor, 0x0F);                                  /* SETL cl (SF != OF) */
    Emit8(cursor, 0x9C);
    EmitModRmReg(cursor, 3, kCl, 0);
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rd), kEcx);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, mips_emit::GprHiOff(d->rd), 0);
    return cursor;
}
