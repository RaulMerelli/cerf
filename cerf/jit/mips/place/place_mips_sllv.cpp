#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* SLLV rd, rt, rs : rd = sext32(rt[31:0] << (rs & 31)), variable shift. x86 SHL
   r/m32, CL masks the count to 5 bits, matching MIPS rs[4:0]. QEMU gen_shift
   OPC_SLLV (translate.c:2850). */
uint8_t* PlaceMipsSllv(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));  /* CL = count */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    Emit8(cursor, 0xD3);                          /* SHL r/m32, CL (D3 /4) */
    EmitModRmReg(cursor, 3, kEax, 4);
    mips_emit::EmitStoreGprSextEax(cursor, d->rd);
    return cursor;
}
