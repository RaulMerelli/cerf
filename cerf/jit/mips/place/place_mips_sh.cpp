#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* SH rt, offset(rs): mem[EA][15:0] = gpr[rt][15:0] (EA must be 2-aligned). No r0
   guard (gpr[0] reads 0, so SH $zero stores 0). */
uint8_t* PlaceMipsSh(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t sext = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEcx, sext);                                       /* ECX = EA */
    EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, mips_emit::GprLoOff(d->rt)); /* EDX = value */
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::StoreHalfHelper));
    return cursor;
}
