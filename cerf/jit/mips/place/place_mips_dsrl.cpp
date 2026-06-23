#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* DSRL rd, rt, sa : rd = gpr[rt] >> sa, 64-bit LOGICAL, sa in [0,31] (DSRL32
   covers 32..63). QEMU gen_shift_imm OPC_DSRL. rd==0 NOP. (rs!=0 would be DROTR,
   not emitted by this MIPS IV kernel.) */
uint8_t* PlaceMipsDsrl(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitDShiftImm(cursor, d->rd, d->rt, static_cast<uint8_t>(d->sa),
                             /*left=*/false, /*outer_ext=*/5);  /* SHR */
    return cursor;
}
