#include <cstddef>

#include "../block_context.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

/* ARM DDI 0406C.c A8.8.18 B (p. A8-335): BranchWritePC(PC + imm32);
   A8.8.25 BL encoding A1 (p. A8-349): LR = PC - 4, targetAddress =
   Align(PC,4) + imm32. */
uint8_t* PlaceBranch(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    if (d->l != 0u) {
        EmitMovBaseDisp32Imm32(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, gprs) +
                                 ArmGpr::kR14 * 4u),
            (d->guest_address + 4u) | (ctx->thumb ? 1u : 0u));
    }
    if (d->n != 0u) {
        /* ARM DDI 0406C.c A8.8.25 BLX T2: SelectInstrSet(ARM). */
        EmitAndBaseDisp32Imm32(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, cpsr)), ~0x20u);
    }
    uint32_t target = ArmPcReadValue(d, ctx) +
                      static_cast<uint32_t>(d->offset);
    if (d->n != 0u) target &= ~3u;
    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + 15u * 4u),
        target);

    cursor = EmitChainToBlock(cursor, ctx, target, 0u);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}
