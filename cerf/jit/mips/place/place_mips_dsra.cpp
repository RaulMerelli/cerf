#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* DSRA rd, rt, sa : rd = gpr[rt] >> sa, 64-bit ARITHMETIC, sa in [0,31] (DSRA32
   covers 32..63). QEMU gen_shift_imm OPC_DSRA (sari). rd==0 NOP. */
uint8_t* PlaceMipsDsra(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitDShiftImm(cursor, d->rd, d->rt, static_cast<uint8_t>(d->sa),
                             /*left=*/false, /*outer_ext=*/7);  /* SAR */
    return cursor;
}
