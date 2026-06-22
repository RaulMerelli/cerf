#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"

/* BEQ rs, rt, offset: branch if the full 64-bit gpr[rs] == gpr[rt]; the delay
   slot runs regardless. */
uint8_t* PlaceMipsBeq(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    const int32_t  pc_off = static_cast<int32_t>(offsetof(MipsCpuState, pc));
    const uint32_t soff = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    const uint32_t btgt = d->guest_address + 4u + (soff << 2);
    const uint32_t fall = d->guest_address + 8u;
    mips_emit::EmitEqBranch64(cursor, d->rs, d->rt, btgt, fall, pc_off,
                             /*take_if_equal=*/true);
    return cursor;
}
