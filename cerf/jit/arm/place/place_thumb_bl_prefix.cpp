#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 10 (p. A7-27). */
uint8_t* PlaceThumbBlPrefix(uint8_t* cursor, DecodedInsn* d,
                            BlockContext* ctx) {
    using namespace x86;

    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + ArmGpr::kR14 * 4u),
        ArmPcReadValue(d, ctx) + static_cast<uint32_t>(d->offset));
    return cursor;
}
