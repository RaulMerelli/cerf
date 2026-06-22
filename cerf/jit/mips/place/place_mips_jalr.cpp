#include "../mips_place_fns.h"

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* JALR rd, rs : btarget = gpr[rs] (read first, so rd aliasing rs still jumps to
   rs); gpr[rd] = sext64(PC + 8); branch_state = register. QEMU OPC_JALR. */
uint8_t* PlaceMipsJalr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitBranchRegister(cursor, d->rs);
    if (d->rd != 0) {
        mips_emit::EmitStoreGprSextImm32(cursor, d->rd, d->guest_address + 8u);
    }
    return cursor;
}
