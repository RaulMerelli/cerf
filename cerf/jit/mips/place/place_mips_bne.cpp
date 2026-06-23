#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* BNE rs, rt, offset : conditional branch, taken if the full 64-bit gpr[rs] !=
   gpr[rt]; the delay slot runs regardless. */
uint8_t* PlaceMipsBne(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    const uint32_t soff = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    const uint32_t btgt = d->guest_address + 4u + (soff << 2);
    mips_emit::EmitBranchCondEq(cursor, d->rs, d->rt, btgt, /*take_if_equal=*/false,
                                MipsBranch::kCond);
    return cursor;
}
