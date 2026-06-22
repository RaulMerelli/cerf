#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* DSRL32 rd, rt, sa : rd = rt >> (sa + 32), logical, full 64-bit -> rd.lo =
   rt.hi >> sa, rd.hi = 0. (QEMU translate.c gen_shift_imm OPC_DSRL32.) The R bit
   (word[21], = rs[0]) selects DROTR32, which shares this funct and is not yet
   implemented - route it to the loud stub. */
uint8_t* PlaceMipsDsrl32(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    if (d->rs & 1u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    if (d->rd == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rt));
    if (d->sa != 0) {
        EmitShrReg32Imm(cursor, kEax, static_cast<uint8_t>(d->sa));
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rd), kEax);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, mips_emit::GprHiOff(d->rd), 0);
    return cursor;
}
