#include "../mips_place_fns.h"

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* JR rs : branch_state = register, btarget = gpr[rs] (low 32), read now before
   the delay slot can clobber rs. QEMU gen_compute_branch OPC_JR. */
uint8_t* PlaceMipsJr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitBranchRegister(cursor, d->rs);
    return cursor;
}
