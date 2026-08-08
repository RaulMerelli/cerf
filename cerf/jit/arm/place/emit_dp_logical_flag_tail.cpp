#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

namespace {

constexpr int32_t CpsrDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, cpsr));
}

}  /* namespace */

/* ARM DDI 0406C.c: flag writeback for the logical data-processing class -
   APSR.C = the Shift_C carry, V unchanged (A8.8.14 p. A8-327, A8.8.241
   p. A8-747); Shift_C(amount == 0) leaves carry_in (A8.4.3 p. A8-293).
   LAHF (SDM Vol. 2A 3-580): EAX<15> = SF, <14> = ZF; shifted into CPSR
   N[31] Z[30] (DDI 0406C.c B1.3.3 p. B1-1148). */
uint8_t* EmitDpLogicalFlagTail(uint8_t* cursor, DpLogicalCarry carry) {
    using namespace x86;

    EmitLahf(cursor);
    EmitShlReg32Imm(cursor, kEax, 16);
    EmitAndRegImm32(cursor, kEax, 0xC0000000u);
    switch (carry) {
    case DpLogicalCarry::kUnchanged:
    case DpLogicalCarry::kClearImm:
        break;
    case DpLogicalCarry::kSetImm:
        EmitOrRegImm32(cursor, kEax, 0x20000000u);
        break;
    case DpLogicalCarry::kFromDl:
        EmitShlReg32Imm(cursor, kEdx, 29);
        EmitOrReg32Reg32(cursor, kEax, kEdx);
        break;
    }
    const uint32_t keep =
        carry == DpLogicalCarry::kUnchanged ? 0x3FFFFFFFu : 0x1FFFFFFFu;
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, CpsrDisp());
    EmitAndRegImm32(cursor, kEcx, keep);
    EmitOrReg32Reg32(cursor, kEcx, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, CpsrDisp(), kEcx);
    return cursor;
}
