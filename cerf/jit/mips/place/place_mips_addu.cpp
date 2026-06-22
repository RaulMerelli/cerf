#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* ADDU rd, rs, rt : rd = sign_extend_32(rs[31:0] + rt[31:0]). No overflow trap. */
uint8_t* PlaceMipsAddu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    mips_emit::EmitLoadGprLo(cursor, kEax, d->rs);
    EmitAddRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    mips_emit::EmitStoreGprSextEax(cursor, d->rd);
    return cursor;
}
