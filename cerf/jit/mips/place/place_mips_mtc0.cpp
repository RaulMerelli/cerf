#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"
#include "../mips_jit.h"
#include "../../cpu/mips_processor_config.h"

/* MTC0 rt, rd : cp0[rd] = gpr[rt] (low 32 bits; MTC0 is a 32-bit move), sel 0.
   Only writable registers the SoC implements are accepted; a read-only,
   unmodelled, or SoC-absent rd (or sel != 0) routes to the loud stub. */
uint8_t* PlaceMipsMtc0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    if ((d->raw & 0x7u) != 0u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    const int32_t off = Cp0RegOffset(d->rd);
    if (off < 0 || !Cp0RegWritable(d->rd) || !ctx->jit->CpuConfig()->HasCp0Reg(d->rd)) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    EmitMovBaseDisp32Reg(cursor, kStateReg, off, kEax);
    return cursor;
}
