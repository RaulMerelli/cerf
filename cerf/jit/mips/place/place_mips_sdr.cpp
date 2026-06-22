#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* SDR rt, offset(rs): unaligned store-doubleword-right. SdrHelper does the
   byte-wise store; the register index is passed (not the 64-bit value). */
uint8_t* PlaceMipsSdr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t sext = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEcx, sext);          /* ECX = EA */
    EmitMovRegImm32(cursor, kEdx, d->rt);         /* EDX = rt index */
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::SdrHelper));
    return cursor;
}
