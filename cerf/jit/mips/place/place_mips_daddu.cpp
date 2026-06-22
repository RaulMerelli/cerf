#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

/* DADDU rd, rs, rt : rd = rs + rt, full 64-bit (no overflow trap). */
uint8_t* PlaceMipsDaddu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitRtypeArith64(cursor, d->rd, d->rs, d->rt, 0x03, 0x13);  /* ADD/ADC */
    return cursor;
}
