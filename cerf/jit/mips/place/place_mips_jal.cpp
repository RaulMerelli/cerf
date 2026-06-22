#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* JAL target : gpr[31] = sign_extend_64((branch PC) + 8); pc = ((PC + 4) &
   0xF0000000) | (target26 << 2). QEMU translate.c gen_compute_branch OPC_JAL. */
uint8_t* PlaceMipsJal(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t pc = static_cast<int32_t>(offsetof(MipsCpuState, pc));
    const uint32_t tgt = ((d->guest_address + 4u) & 0xF0000000u) | (d->target << 2);
    mips_emit::EmitStoreGprSextImm32(cursor, 31, d->guest_address + 8u);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, pc, tgt);
    return cursor;
}
