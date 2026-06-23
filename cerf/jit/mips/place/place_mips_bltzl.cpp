#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* BLTZL rs, offset : branch-likely, taken if (int64)gpr[rs] < 0; not-taken
   nullifies the delay slot. (QEMU gen_compute_branch OPC_BLTZL.) */
uint8_t* PlaceMipsBltzl(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    const uint32_t soff = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    const uint32_t btgt = d->guest_address + 4u + (soff << 2);
    mips_emit::EmitBranchCondSign(cursor, d->rs, btgt, /*take_if_neg=*/true,
                                  MipsBranch::kCondLikely);
    return cursor;
}
