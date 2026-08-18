#include <cstddef>

#include "../arm_emit_services.h"
#include "../arm_exception_frame.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

uint8_t* PlaceRfe(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    /* MOV ECX, [ESI + gprs[Rn]] - rn_value into __fastcall arg 1. */
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rn * 4u));

    /* MOV EDX, <encoded> - pack P/U/W/Rn into __fastcall arg 2:
       bit 7 = P, bit 6 = U, bit 5 = W, bits 4:0 = Rn. */
    const uint32_t encoded =
        (static_cast<uint32_t>(d->p) << 7) |
        (static_cast<uint32_t>(d->u) << 6) |
        (static_cast<uint32_t>(d->w) << 5) |
        (static_cast<uint32_t>(d->rn) & 0x1Fu);
    EmitMovRegImm32(cursor, kEdx, encoded);

    EmitPush32(cursor,
        static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(ctx->emit->ExceptionFrame())));

    EmitCall(cursor, reinterpret_cast<void*>(&ArmExceptionFrame::RfeHelper));

    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* aborted = EmitJnzLabel32(cursor);
    cursor = PlaceR15ModifiedHelper(cursor, d, ctx);
    FixupLabel32(aborted, cursor);
    return EmitAbortDataTail(cursor, d, ctx);
}
