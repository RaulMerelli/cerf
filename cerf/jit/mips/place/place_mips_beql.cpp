#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* BEQL rs, rt, offset : branch-likely, taken if gpr[rs] == gpr[rt] (64-bit). If
   NOT taken the delay slot is nullified (the engine's kCondLikely path). bcond
   computed identically to BEQ (QEMU gen_compute_branch OPC_BEQL -> likely). */
uint8_t* PlaceMipsBeql(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    const uint32_t soff = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    const uint32_t btgt = d->guest_address + 4u + (soff << 2);
    mips_emit::EmitBranchCondEq(cursor, d->rs, d->rt, btgt, /*take_if_equal=*/true,
                                MipsBranch::kCondLikely);
    return cursor;
}
