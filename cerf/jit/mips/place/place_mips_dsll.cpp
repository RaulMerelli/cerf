#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* DSLL rd, rt, sa : rd = gpr[rt] << sa, 64-bit, sa in [0,31] (DSLL32 covers
   32..63). QEMU gen_shift_imm OPC_DSLL. rd==0 NOP. */
uint8_t* PlaceMipsDsll(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitDShiftImm(cursor, d->rd, d->rt, static_cast<uint8_t>(d->sa),
                             /*left=*/true, /*outer_ext=*/4);  /* SHL */
    return cursor;
}
