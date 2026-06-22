#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

uint8_t* PlaceClz(uint8_t*      cursor,
                  DecodedInsn*  d,
                  BlockContext* /*ctx*/) {
    using namespace x86;

    /* MOV EAX, [ESI + GPRs[Rm]] */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rm * 4));

    /* MOV ECX, 32  (preload Rm==0 result so the zero branch skips
       the BSR + SUB and stores 32 directly). */
    EmitMovRegImm32(cursor, kEcx, 32);

    /* TEST EAX, EAX */
    EmitTestRegReg(cursor, kEax, kEax);

    /* JZ .done */
    uint8_t* jz_done = EmitJzLabel(cursor);

    /* BSR EDX, EAX  →  0F BD /r  (reg = EDX, r/m = EAX, mod = 3). */
    Emit8(cursor, 0x0F);
    Emit8(cursor, 0xBD);
    EmitModRmReg(cursor, /*mod=*/3, /*rm=*/kEax, /*reg=*/kEdx);

    /* MOV ECX, 31 */
    EmitMovRegImm32(cursor, kEcx, 31);

    /* SUB ECX, EDX */
    EmitSubReg32Reg32(cursor, kEcx, kEdx);

    /* .done: */
    FixupLabel(jz_done, cursor);

    /* MOV [ESI + GPRs[Rd]], ECX */
    EmitMovBaseDisp32Reg(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4),
        kEcx);

    return cursor;
}
