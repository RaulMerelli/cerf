#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* BGEZAL rs, offset : link ra = pc+8 (unconditionally), then branch if
   (int64)gpr[rs] >= 0. The condition reads gpr[rs] BEFORE the link, matching QEMU
   gen_compute_branch (bcond from gpr[rs]; cpu_gpr[31] written last). */
uint8_t* PlaceMipsBgezal(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    const uint32_t soff = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    const uint32_t btgt = d->guest_address + 4u + (soff << 2);
    mips_emit::EmitBranchCondSign(cursor, d->rs, btgt, /*take_if_neg=*/false,
                                  MipsBranch::kCond, d->length);
    mips_emit::EmitStoreGprSextImm32(cursor, 31, d->guest_address + 8u);
    return cursor;
}
