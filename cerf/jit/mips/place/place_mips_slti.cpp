#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

/* SLTI rt, rs, imm : rt = (rs < sext64(imm16)) signed ? 1 : 0. */
uint8_t* PlaceMipsSlti(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitSltImm64(cursor, d->rt, d->rs, d->imm, 0x9C);  /* SETL: signed < */
    return cursor;
}
