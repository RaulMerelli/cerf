#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* ARM DDI 0406C.c A8.8.18 B (p. A8-335): BranchWritePC(PC + imm32);
   A8.8.25 BL encoding A1 (p. A8-349): LR = PC - 4, targetAddress =
   Align(PC,4) + imm32; A2.3 (p. A2-45): the ARM-state PC reads as the
   instruction address + 8. */
uint8_t* PlaceBranch(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    if (d->l != 0u) {
        EmitMovBaseDisp32Imm32(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, gprs) +
                                 ArmGpr::kR14 * 4u),
            d->guest_address + 4u);
    }
    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + 15u * 4u),
        d->guest_address + 8u + static_cast<uint32_t>(d->offset));
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}
