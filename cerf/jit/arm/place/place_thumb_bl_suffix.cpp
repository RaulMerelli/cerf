#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 11 (p. A7-27). */
uint8_t* PlaceThumbBlSuffix(uint8_t* cursor, DecodedInsn* d,
                            BlockContext* ctx) {
    using namespace x86;

    constexpr int32_t kLrDisp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + ArmGpr::kR14 * 4u);

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kLrDisp);
    if (d->offset != 0) {
        EmitAddRegImm32(cursor, kEax, static_cast<uint32_t>(d->offset));
    }
    if (d->n != 0u) {
        /* ARM DDI 0100I A7.1.17 BLX (1): H == 01 selects ARM state and the
           destination is word-aligned. */
        EmitAndRegImm32(cursor, kEax, ~3u);
        EmitAndBaseDisp32Imm32(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, cpsr)), ~0x20u);
    }
    EmitMovBaseDisp32Imm32(cursor, kStateReg, kLrDisp,
                           (d->guest_address + 2u) | 1u);
    EmitMovBaseDisp32Reg(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + ArmGpr::kR15 * 4u),
        kEax);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}
