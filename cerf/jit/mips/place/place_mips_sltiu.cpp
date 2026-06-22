#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

/* SLTIU rt, rs, imm : rt = (rs < sext64(imm16)) unsigned ? 1 : 0. The immediate
   is sign-extended to 64 bits, then compared UNSIGNED (MIPS quirk). */
uint8_t* PlaceMipsSltiu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitSltImm64(cursor, d->rt, d->rs, d->imm, 0x92);  /* SETB: unsigned < */
    return cursor;
}
