#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* DADD rd, rs, rt : rd = rs + rt, 64-bit; a signed 64-bit overflow raises Integer
   Overflow and writes nothing (the only difference from DADDU; QEMU gen_arith
   OPC_DADD). ADD low (sets CF) then ADC high -> OF = 64-bit overflow. */
uint8_t* PlaceMipsDadd(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    mips_emit::EmitTrappingArith64RR(cursor, d->rd, d->rs, d->rt,
        /*lo_op=ADD*/ 0x03, /*hi_op=ADC*/ 0x13, ctx->jit,
        reinterpret_cast<void*>(&MipsJit::ArithOverflowHelper), d->guest_address);
    return cursor;
}
