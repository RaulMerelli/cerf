#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* DSUB rd, rs, rt : rd = rs - rt, 64-bit; a signed 64-bit overflow raises Integer
   Overflow and writes nothing (the only difference from DSUBU; QEMU gen_arith
   OPC_DSUB). SUB low (sets CF) then SBB high -> OF = 64-bit overflow. */
uint8_t* PlaceMipsDsub(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    mips_emit::EmitTrappingArith64RR(cursor, d->rd, d->rs, d->rt,
        /*lo_op=SUB*/ 0x2B, /*hi_op=SBB*/ 0x1B, ctx->jit,
        reinterpret_cast<void*>(&MipsJit::ArithOverflowHelper), d->guest_address);
    return cursor;
}
