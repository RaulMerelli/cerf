#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

/* ARM DDI 0406C.c A8.8.27 CBZ/CBNZ T1 (p. A8-354): flags are unchanged;
   branch to PC+imm32 when the selected zero comparison succeeds. */
uint8_t* PlaceThumbCompareBranch(uint8_t* cursor, DecodedInsn* d,
                                 BlockContext* ctx) {
    using namespace x86;
    const int32_t rn_disp = static_cast<int32_t>(
        offsetof(ArmCpuState, gprs) + d->rn * 4u);
    const int32_t pc_disp = static_cast<int32_t>(
        offsetof(ArmCpuState, gprs) + ArmGpr::kR15 * 4u);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rn_disp);
    EmitCmpRegImm32(cursor, kEax, 0u);
    uint8_t* not_taken = d->n != 0u ? EmitJzLabel32(cursor)
                                     : EmitJnzLabel32(cursor);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, pc_disp,
                           d->guest_address + 4u +
                           static_cast<uint32_t>(d->offset));
    uint8_t* done = EmitJmpLabel32(cursor);
    FixupLabel32(not_taken, cursor);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, pc_disp,
                           d->guest_address + 2u);
    FixupLabel32(done, cursor);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}
