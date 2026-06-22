#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* DADDIU rt, rs, imm : rt = rs + sext64(imm16), full 64-bit, no overflow trap.
   64-bit add on a 32-bit host: the low add sets CF, the MOVs preserve it, and
   the high-word ADC folds it in - so nothing flag-clobbering may go between the
   add and the adc. imm_hi is the sign extension of imm16: 0 or -1. */
uint8_t* PlaceMipsDaddiu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rt == 0) {
        return cursor;                    /* result discarded; DADDIU never traps */
    }
    const uint16_t imm16 = static_cast<uint16_t>(d->imm);
    if (imm16 == 0) {                     /* rt = rs (64-bit copy); no add, no CF */
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
        EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rt), kEax);
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs));
        EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprHiOff(d->rt), kEax);
        return cursor;
    }
    const uint32_t imm_lo = static_cast<uint32_t>(static_cast<int32_t>(
                                static_cast<int16_t>(imm16)));
    const uint8_t imm_hi8 = (imm16 & 0x8000u) ? 0xFFu : 0x00u;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEax, imm_lo);                          /* CF = carry */
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rt), kEax);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs));
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprHiOff(d->rt), kEax);
    EmitAdcBaseDisp32Imm8(cursor, kStateReg, mips_emit::GprHiOff(d->rt), imm_hi8);
    return cursor;
}
