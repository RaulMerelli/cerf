#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* DSRLV rd, rt, rs : rd = gpr[rt] >> (gpr[rs] & 63), 64-bit LOGICAL (QEMU
   gen_shift OPC_DSRLV). __fastcall: rd index in ECX, rt index in EDX, rs index
   then jit on the stack. */
uint8_t* PlaceMipsDsrlv(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx, d->rd);
    EmitMovRegImm32(cursor, kEdx, d->rt);
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitPush32(cursor, d->rs);
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::DsrlvHelper));
    return cursor;
}
