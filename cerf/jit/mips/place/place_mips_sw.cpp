#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* SW rt, offset(rs): mem[gpr[rs] + sext(imm16)][31:0] = gpr[rt][31:0], 32-bit
   addressing. No r0 guard (unlike GPR-writing place fns): r0 reads as 0, so
   SW $zero correctly stores zero. */
uint8_t* PlaceMipsSw(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t sext =
        static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(d->imm)));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEcx, sext);                                    /* ECX = EA */
    EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, mips_emit::GprLoOff(d->rt)); /* EDX = value */
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::StoreWordHelper));
    return cursor;
}
