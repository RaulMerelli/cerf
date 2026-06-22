#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* SRLV rd, rt, rs : rd = sext32(rt[31:0] >> (rs & 31)), logical variable shift.
   x86 SHR r/m32, CL masks the count to 5 bits, matching the MIPS rs[4:0]. */
uint8_t* PlaceMipsSrlv(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));  /* CL = count */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    Emit8(cursor, 0xD3);                          /* SHR r/m32, CL (D3 /5) */
    EmitModRmReg(cursor, 3, kEax, 5);
    mips_emit::EmitStoreGprSextEax(cursor, d->rd);
    return cursor;
}
