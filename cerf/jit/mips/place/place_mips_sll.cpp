#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

/* SLL rd, rt, sa : rd = sext32(rt[31:0] << sa). The true NOP (SLL r0,r0,0) is
   caught before this in SelectPlaceFn. */
uint8_t* PlaceMipsSll(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitShiftImm32Sext(cursor, d->rd, d->rt, d->sa, 4);  /* SHL (0xC1 /4) */
    return cursor;
}
