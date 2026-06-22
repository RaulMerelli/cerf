#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* SLTU rd, rs, rt : rd = (rs < rt) unsigned, 64-bit ? 1 : 0. The borrow (CF)
   left by a 64-bit subtract (SUB low, SBB high) IS the unsigned less-than;
   SETB captures it into a pre-zeroed ECX, so nothing flag-clobbering may sit
   between the subtract and the SETB. Result is 0/1, high word always 0. */
uint8_t* PlaceMipsSltu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
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
    EmitSetcReg8(cursor, kCl);                            /* CL = CF = (rs < rt unsigned) */
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rd), kEcx);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, mips_emit::GprHiOff(d->rd), 0);
    return cursor;
}
