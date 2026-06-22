#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* DIVU rs, rt : LO/HI = sext32 of the unsigned quotient/remainder of rs[31:0] by
   rt[31:0]. A zero divisor would trap the host DIV (#DE); MIPS leaves the result
   UNPREDICTABLE-no-trap, so substitute divisor 1 as QEMU does (translate.c
   gen_muldiv OPC_DIVU :3299). */
uint8_t* PlaceMipsDivu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t kLoLo = static_cast<int32_t>(offsetof(MipsCpuState, lo));
    const int32_t kLoHi = kLoLo + 4;
    const int32_t kHiLo = static_cast<int32_t>(offsetof(MipsCpuState, hi));
    const int32_t kHiHi = kHiLo + 4;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rt));  /* ECX = divisor */
    EmitTestRegReg(cursor, kEcx, kEcx);
    uint8_t* j_nz = EmitJnzLabel(cursor);
    EmitMovRegImm32(cursor, kEcx, 1);              /* divisor 0 -> 1 (QEMU guard) */
    FixupLabel(j_nz, cursor);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));  /* EAX = dividend */
    EmitXorRegReg(cursor, kEdx, kEdx);             /* EDX = 0 (unsigned high half) */
    Emit8(cursor, 0xF7);                           /* DIV ecx (F7 /6): EAX=quot, EDX=rem */
    EmitModRmReg(cursor, 3, kEcx, 6);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoLo, kEax);  /* LO.lo = quotient */
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiLo, kEdx);  /* HI.lo = remainder */
    Emit8(cursor, 0x99);                           /* CDQ: EDX = sext(quotient) (EAX unchanged) */
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoHi, kEdx);  /* LO.hi = sext(quotient) */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kHiLo);  /* EAX = remainder */
    Emit8(cursor, 0x99);                           /* CDQ: EDX = sext(remainder) */
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiHi, kEdx);  /* HI.hi = sext(remainder) */
    return cursor;
}
