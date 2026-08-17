#include <cstddef>

#include "../block_context.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

/* ARM DDI 0406C.c A8.8.25 BL, BLX (immediate) Operation (p. A8-349):
   LR = PC<31:1>:'1'; targetAddress = Align(PC,4) + imm32 when targetInstrSet
   is InstrSet_ARM; SelectInstrSet then BranchWritePC. */
uint8_t* PlaceThumbBlxImm(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + ArmGpr::kR14 * 4u),
        (d->guest_address + d->length) | 1u);
    EmitMovRegImm32(cursor, kEax,
        (ArmPcReadValue(d, ctx) & ~3u) + static_cast<uint32_t>(d->offset));
    cursor = EmitArmInterworkingFullEax(cursor);
    EmitMovBaseDisp32Reg(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + 15u * 4u), kEax);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}
