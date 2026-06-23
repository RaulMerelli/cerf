#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* LDR rt, offset(rs): unaligned load-doubleword-right (64-bit). LdrHelper does the
   merge; the register index is passed (the helper reads and writes gpr[rt]).
   (Doubleword twin of LWR.) */
uint8_t* PlaceMipsLdr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t sext = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEcx, sext);          /* ECX = EA */
    EmitMovRegImm32(cursor, kEdx, d->rt);         /* EDX = rt index */
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::LdrHelper));
    return cursor;
}
