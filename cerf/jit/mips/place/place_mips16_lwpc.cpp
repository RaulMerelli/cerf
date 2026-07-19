#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* MIPS16 LW rx, offset(pc) (U15509EJ2V0UM Table 3-14 p71): EA = masked BasePC
   + imm<<2, resolved into d->target at decode; the loaded word "is further
   sign extended to 64 bits". rx encodes $2-$7/$16/$17 (Table 3-1 p55), never
   r0. */
uint8_t* PlaceMips16Lwpc(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx, d->target);
    EmitMovRegImm32(cursor, kEdx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::LoadWordHelper));
    mips_emit::EmitStoreGprSextEax(cursor, d->rt);
    return cursor;
}
