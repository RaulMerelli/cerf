#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* ANDI rt, rs, imm : rt = rs & zext64(imm16). The immediate is ZERO-extended,
   so the result's high 32 bits are always 0 (unlike ORI/XORI, which pass the
   high word through). */
uint8_t* PlaceMipsAndi(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rt == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAndRegImm32(cursor, kEax, d->imm);    /* d->imm is the zero-extended imm16 */
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rt), kEax);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, mips_emit::GprHiOff(d->rt), 0);
    return cursor;
}
