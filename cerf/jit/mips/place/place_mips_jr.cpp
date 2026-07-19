#include "../mips_place_fns.h"

#include "../../../cpu/mips_processor_config.h"
#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"

/* JR rs : branch_state = register, btarget = gpr[rs] (low 32), read now before
   the delay slot can clobber rs. QEMU gen_compute_branch OPC_JR. */
uint8_t* PlaceMipsJr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    mips_emit::EmitBranchRegister(cursor, d->rs, ctx->jit->CpuConfig()->HasMips16(),
                                  d->length);
    return cursor;
}
