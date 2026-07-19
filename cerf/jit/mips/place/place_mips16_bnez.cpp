#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* MIPS16 BNEZ rx / BTNEZ (T=$24) (U15509EJ2V0UM Table 3-19 p83): taken iff
   the 64-bit gpr[rs] != 0; no delay slot (ibid. + 3.8.3 p70). */
uint8_t* PlaceMips16Bnez(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    constexpr int32_t kPcOff = static_cast<int32_t>(offsetof(MipsCpuState, pc));
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    Emit8(cursor, 0x0B);                                /* OR eax, [esi+rs.hi] */
    EmitModRmReg(cursor, 2, kStateReg, kEax);
    Emit32(cursor, static_cast<uint32_t>(mips_emit::GprHiOff(d->rs)));
    uint8_t* j_z = EmitJzLabel(cursor);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, kPcOff, d->target);
    uint8_t* j_done = EmitJmpLabel(cursor);
    FixupLabel(j_z, cursor);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, kPcOff,
                           d->guest_address + d->length);
    FixupLabel(j_done, cursor);
    return cursor;
}
