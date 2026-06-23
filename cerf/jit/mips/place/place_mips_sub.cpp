#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* SUB rd, rs, rt : rd = sext32(rs[31:0] - rt[31:0]); a signed 32-bit overflow
   raises Integer Overflow and writes nothing (twin of ADD; QEMU OPC_SUB). */
uint8_t* PlaceMipsSub(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitSubRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));  /* sets OF */
    mips_emit::EmitTrappingArith32Tail(cursor, d->rd, ctx->jit,
        reinterpret_cast<void*>(&MipsJit::ArithOverflowHelper), d->guest_address);
    return cursor;
}
