#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* ARM DDI 0406C.c A8.8.27 BX (p. A8-353): BXWritePC(R[m]). */
uint8_t* PlaceBxImpl(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx,
                     bool /*is_call*/) {
    using namespace x86;

    if (d->rm == ArmGpr::kR15) {
        EmitMovRegImm32(cursor, kEax, ArmPcReadValue(d, ctx));
    } else {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rm * 4u));
    }
    cursor = EmitArmInterworkingFullEax(cursor);
    EmitMovBaseDisp32Reg(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + 15u * 4u), kEax);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}

uint8_t* PlaceBx(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    return PlaceBxImpl(cursor, d, ctx, false);
}
