#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

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

    /* SETcc (SDM Vol. 2B 4-620): "Flags Affected: None". */
    EmitSetccBaseDisp32(cursor, kSetS, kStateReg, ArmNfDisp());
    EmitSetccBaseDisp32(cursor, kSetZ, kStateReg, ArmZfDisp());
    EmitSetccBaseDisp32(cursor, borrow_form ? kSetNc : kSetC, kStateReg,
                        ArmCfDisp());
    EmitSetccBaseDisp32(cursor, kSetO, kStateReg, ArmVfDisp());
    return cursor;
}
