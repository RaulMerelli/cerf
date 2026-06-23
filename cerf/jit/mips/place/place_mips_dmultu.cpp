#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* DMULTU rs, rt : {HI,LO} = gpr[rs] * gpr[rt], 128-bit unsigned (QEMU gen_muldiv
   OPC_DMULTU). The 64x64->128 multiply runs in DmultuHelper. No GPR dest.
   __fastcall: rs index in ECX, rt index in EDX, jit on the stack. */
uint8_t* PlaceMipsDmultu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx, d->rs);
    EmitMovRegImm32(cursor, kEdx, d->rt);
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::DmultuHelper));
    return cursor;
}
