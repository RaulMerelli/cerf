#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* BLEZL rs, offset : branch-likely, taken if (int64)gpr[rs] <= 0; not-taken
   nullifies the delay slot. (QEMU gen_compute_branch OPC_BLEZL.) */
uint8_t* PlaceMipsBlezl(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    const uint32_t soff = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    const uint32_t btgt = d->guest_address + 4u + (soff << 2);
    mips_emit::EmitBranchCondLez(cursor, d->rs, btgt, MipsBranch::kCondLikely);
    return cursor;
}
