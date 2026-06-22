#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

/* OR rd, rs, rt : rd = rs | rt, full 64-bit (both halves). */
uint8_t* PlaceMipsOr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitRtypeBitwise64(cursor, d->rd, d->rs, d->rt, 0x0B);  /* OR r32,r/m32 */
    return cursor;
}
