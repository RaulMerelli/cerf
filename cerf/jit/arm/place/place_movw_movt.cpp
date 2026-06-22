#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

uint8_t* PlaceMovw(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext* /*ctx*/) {
    using namespace x86;
    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u),
        d->immediate);
    return cursor;
}

uint8_t* PlaceMovt(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext* /*ctx*/) {
    using namespace x86;
    const int32_t rd_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
    EmitAndRegImm32     (cursor, kEax, 0x0000FFFFu);
    EmitMovRegImm32     (cursor, kEcx, d->immediate << 16);
    EmitOrReg32Reg32    (cursor, kEax, kEcx);
    EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
    return cursor;
}
