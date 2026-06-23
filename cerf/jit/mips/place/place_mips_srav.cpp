#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* SRAV rd, rt, rs : rd = sext32((int32)rt[31:0] >> (rs & 31)), arithmetic shift.
   x86 SAR r/m32, CL masks the count to 5 bits = MIPS rs[4:0] (twin of SRLV with
   SAR; QEMU gen_shift OPC_SRAV andi 0x1f + sar). */
uint8_t* PlaceMipsSrav(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));  /* CL = count */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    Emit8(cursor, 0xD3);                          /* SAR r/m32, CL (D3 /7) */
    EmitModRmReg(cursor, 3, kEax, 7);
    mips_emit::EmitStoreGprSextEax(cursor, d->rd);
    return cursor;
}
