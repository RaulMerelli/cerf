#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

/* SRL rd, rt, sa : rd = sext32(rt[31:0] >> sa), logical. The R bit (word[21] =
   rs[0]) selects ROTR, which shares this funct and is not yet implemented -
   route it to the loud stub. */
uint8_t* PlaceMipsSrl(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    if (d->rs & 1u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    mips_emit::EmitShiftImm32Sext(cursor, d->rd, d->rt, d->sa, 5);  /* SHR (0xC1 /5) */
    return cursor;
}
