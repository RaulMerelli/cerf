#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* JALR rd, rs : gpr[rd] = sign_extend_64((branch PC) + 8); pc = gpr[rs] (low
   32). rs is read before the link store so rd aliasing rs still jumps to rs. */
uint8_t* PlaceMipsJalr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t pc = static_cast<int32_t>(offsetof(MipsCpuState, pc));
    mips_emit::EmitLoadGprLo(cursor, kEax, d->rs);
    if (d->rd != 0) {
        mips_emit::EmitStoreGprSextImm32(cursor, d->rd, d->guest_address + 8u);
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, pc, kEax);
    return cursor;
}
