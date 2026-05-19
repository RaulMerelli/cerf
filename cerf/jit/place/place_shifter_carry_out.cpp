#include <cstddef>

#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceShifterCarryOut(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* /* ctx */) {
    using namespace x86;
    if (d->i) {
        const uint32_t rotate_imm = (d->operand2 >> 8) & 0xFu;
        if (rotate_imm == 0u) {
            EmitBtBaseDisp32Imm(cursor, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, x86_flags)), 0);
        } else {
            if ((d->reserved3 >> 31) & 1u) {
                Emit8(cursor, 0xF9);  /* STC */
            } else {
                Emit8(cursor, 0xF8);  /* CLC */
            }
        }
    } else {
        /* ROL CL, 8 — C0 /0 imm8 (shifts ARM CF in CL bit 0 into x86 CF). */
        Emit8(cursor, 0xC0); EmitModRmReg(cursor, 3, kCl, 0); Emit8(cursor, 8);
    }
    return cursor;
}
