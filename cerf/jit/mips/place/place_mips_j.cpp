#include "../mips_place_fns.h"

#include <cstddef>

#include "../mips_block_context.h"
#include "../mips_cpu_state.h"
#include "../../x86_emit.h"

/* J target : pc = ((PC + 4) & 0xF0000000) | (target26 << 2). The high 4 bits
   come from the delay-slot address (branch PC + 4); the target is a 28-bit
   region offset. The whole value is a compile-time constant. QEMU translate.c
   gen_compute_branch OPC_J: btgt = ((pc_next + insn_bytes) & 0xF0000000) | offset. */
uint8_t* PlaceMipsJ(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t pc = static_cast<int32_t>(offsetof(MipsCpuState, pc));
    const uint32_t tgt = ((d->guest_address + 4u) & 0xF0000000u) | (d->target << 2);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, pc, tgt);
    return cursor;
}
