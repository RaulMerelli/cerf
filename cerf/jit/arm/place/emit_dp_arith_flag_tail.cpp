#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

namespace {

constexpr int32_t CpsrDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, cpsr));
}

}  /* namespace */

/* ARM DDI 0406C.c: flag writeback for the AddWithCarry data-processing
   class (A8.8.7 Operation, p. A8-313). In: the x86 arithmetic flags of
   the operation. */
uint8_t* EmitDpArithFlagTail(uint8_t* cursor, DecodedInsn* d) {
    using namespace x86;

    /* B9.3.20 (p. B9-2013): SUB/RSB/SBC/RSC are the
       AddWithCarry(..., NOT(y), ...) forms; CMP is the same form per
       A8.8.37 (p. A8-371); A2.2.1 (p. A2-43): their carry_out is
       NOT-borrow, the inverse of the x86 CF. */
    const uint32_t opcode      = d->op1;
    const bool     borrow_form = opcode == 2u || opcode == 3u ||
                                 opcode == 6u || opcode == 7u ||
                                 opcode == 10u;

    /* LAHF (SDM Vol. 2A 3-580): EAX<15> = SF, <14> = ZF, <8> = CF;
       SETO (Vol. 2B 4-619): AL = OF. Shift each into CPSR
       N[31] Z[30] C[29] V[28] (DDI 0406C.c B1.3.3 p. B1-1148). */
    EmitLahf(cursor);
    EmitSetoReg8(cursor, kAl);
    EmitMovRegReg(cursor, kEcx, kEax);
    EmitMovRegReg(cursor, kEdx, kEax);
    EmitShlReg32Imm(cursor, kEax, 16);
    EmitAndRegImm32(cursor, kEax, 0xC0000000u);
    EmitShlReg32Imm(cursor, kEcx, 21);
    EmitAndRegImm32(cursor, kEcx, 0x20000000u);
    EmitOrReg32Reg32(cursor, kEax, kEcx);
    EmitShlReg32Imm(cursor, kEdx, 28);
    EmitAndRegImm32(cursor, kEdx, 0x10000000u);
    EmitOrReg32Reg32(cursor, kEax, kEdx);
    if (borrow_form) {
        EmitXorRegImm32(cursor, kEax, 0x20000000u);
    }
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, CpsrDisp());
    EmitAndRegImm32(cursor, kEcx, 0x0FFFFFFFu);
    EmitOrReg32Reg32(cursor, kEcx, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, CpsrDisp(), kEcx);
    return cursor;
}
