#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* JR rs : pc = gpr[rs] (low 32 = the 32-bit target address). Target read here,
   before the delay slot (emitted next in the block) executes. */
uint8_t* PlaceMipsJr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t pc = static_cast<int32_t>(offsetof(MipsCpuState, pc));
    mips_emit::EmitLoadGprLo(cursor, kEax, d->rs);
    EmitMovBaseDisp32Reg(cursor, kStateReg, pc, kEax);
    return cursor;
}
