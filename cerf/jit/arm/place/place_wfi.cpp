#include <cstddef>

#include "../arm_jit.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* ARMv7 WFI hint, encoding A1 (ARM ARM DDI 0406C.c §A8.8.425, page A8-1106). */
uint8_t* PlaceWfi(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    (void)d;
    EmitMovRegImm32(cursor, kEcx,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmJit::WfiHelper));
    return cursor;
}
