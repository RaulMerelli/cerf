#include <cstdint>

#include "../decoded_insn.h"
#include "../place_fns.h"

uint8_t* EmitVfpRegisterTransfer(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* ctx) {
    /* VMRS / VMSR — VFP system register R/W (cp_opc=7, CRm=0, op2=0). */
    if (d->cp_opc == 7 && d->crm == 0 && d->cp == 0) {
        return EmitVfpSystemRegTransfer(cursor, d, ctx);
    }

    /* VMOV Sn <-> Rt — single-register transfer (cp_opc=0, CRm=0,
       op2 low bits = 0; cp bit 2 = Sn extension). */
    if (d->cp_opc == 0 && d->crm == 0 && (d->cp & 3u) == 0u) {
        return EmitVfpSingleMove(cursor, d, ctx);
    }

    return EmitRaiseUndAndReturn(cursor, d, ctx);
}
