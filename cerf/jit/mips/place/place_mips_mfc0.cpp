#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* MFC0 rt, rd : gpr[rt] = sext64(cp0[rd]) - a 32-bit CP0 read sign-extended to
   64 bits (QEMU translate.c gen_mfc0_load32: ext_i32_tl), sel 0. An unmodelled
   rd or sel != 0 routes to the loud stub. */
uint8_t* PlaceMipsMfc0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    if ((d->raw & 0x7u) != 0u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    const int32_t off = Cp0RegOffset(d->rd);
    if (off < 0) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    if (d->rt == 0) {
        return cursor;                /* read discarded; CP0 reads have no side effect */
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, off);   /* EAX = cp0[rd] (32-bit) */
    mips_emit::EmitStoreGprSextEax(cursor, d->rt);         /* sext64 -> gpr[rt] */
    return cursor;
}
