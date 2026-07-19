#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../../x86_emit.h"

/* MIPS16 B (U15509EJ2V0UM p83): unconditional, NO delay slot (3.8.3 p70); the
   decoder resolved the target into d->target. ends_block: codegen emits the
   ret and suppresses the straight-line pc override. */
uint8_t* PlaceMips16B(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    x86::EmitMovBaseDisp32Imm32(cursor, x86::kStateReg,
                                static_cast<int32_t>(offsetof(MipsCpuState, pc)),
                                d->target);
    return cursor;
}
