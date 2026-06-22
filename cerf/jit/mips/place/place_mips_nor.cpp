#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* NOR rd, rs, rt : rd = ~(rs | rt), full 64-bit (both halves). Compute rs|rt
   via the shared bitwise helper, then NOT each half in place (NOT r/m32 =
   0xF7 /2). */
uint8_t* PlaceMipsNor(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    mips_emit::EmitRtypeBitwise64(cursor, d->rd, d->rs, d->rt, 0x0B);  /* rd = rs | rt */
    if (d->rd != 0) {
        Emit8(cursor, 0xF7);                                  /* NOT dword [esi+rd.lo] */
        EmitModRmReg(cursor, 2, kStateReg, 2);
        Emit32(cursor, static_cast<uint32_t>(mips_emit::GprLoOff(d->rd)));
        Emit8(cursor, 0xF7);                                  /* NOT dword [esi+rd.hi] */
        EmitModRmReg(cursor, 2, kStateReg, 2);
        Emit32(cursor, static_cast<uint32_t>(mips_emit::GprHiOff(d->rd)));
    }
    return cursor;
}
