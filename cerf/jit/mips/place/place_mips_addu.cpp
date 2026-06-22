#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

/* ADDU rd, rs, rt : rd = sext64(rs[31:0] + rt[31:0]). No overflow trap. */
uint8_t* PlaceMipsAddu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitRtypeArith32Sext(cursor, d->rd, d->rs, d->rt, 0x03);  /* ADD r32,r/m32 */
    return cursor;
}
