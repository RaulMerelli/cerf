#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* DIV rs, rt : LO=quotient, HI=remainder (signed, sext32). x86 IDIV (#DE) faults
   on divisor 0 and on INT_MIN/-1; both are UNPREDICTABLE-no-trap on MIPS, so
   substitute divisor 1 there (QEMU gen_muldiv OPC_DIV translate.c:3275-3291),
   yielding LO=rs/INT_MIN, HI=0 - matching QEMU. */
uint8_t* PlaceMipsDiv(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t kLoLo = static_cast<int32_t>(offsetof(MipsCpuState, lo));
    const int32_t kLoHi = kLoLo + 4;
    const int32_t kHiLo = static_cast<int32_t>(offsetof(MipsCpuState, hi));
    const int32_t kHiHi = kHiLo + 4;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rt));  /* ECX = divisor */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));  /* EAX = dividend */

    /* divisor 0 -> 1; else overflow (dividend INT_MIN && divisor -1) -> 1. */
    EmitTestRegReg(cursor, kEcx, kEcx);
    uint8_t* j_chk_ovf = EmitJnzLabel(cursor);     /* ECX != 0 -> check overflow */
    EmitMovRegImm32(cursor, kEcx, 1);              /* div0 -> divisor 1 */
    uint8_t* j_div_a = EmitJmpLabel(cursor);
    FixupLabel(j_chk_ovf, cursor);
    EmitCmpRegImm32(cursor, kEax, 0x80000000u);    /* dividend == INT_MIN ? */
    uint8_t* j_div_b = EmitJnzLabel(cursor);       /* no -> divide as-is */
    EmitCmpRegImm32(cursor, kEcx, 0xFFFFFFFFu);    /* divisor == -1 ? */
    uint8_t* j_div_c = EmitJnzLabel(cursor);       /* no -> divide as-is */
    EmitMovRegImm32(cursor, kEcx, 1);              /* overflow -> divisor 1 */
    FixupLabel(j_div_a, cursor);
    FixupLabel(j_div_b, cursor);
    FixupLabel(j_div_c, cursor);

    Emit8(cursor, 0x99);                            /* CDQ: EDX:EAX = sext(dividend) */
    Emit8(cursor, 0xF7);                            /* IDIV ecx (F7 /7): EAX=quot, EDX=rem */
    EmitModRmReg(cursor, 3, kEcx, 7);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoLo, kEax);  /* LO.lo = quotient */
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiLo, kEdx);  /* HI.lo = remainder */
    Emit8(cursor, 0x99);                            /* CDQ: EDX = sext(quotient) */
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoHi, kEdx);  /* LO.hi = sext(quotient) */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kHiLo);  /* EAX = remainder */
    Emit8(cursor, 0x99);                            /* CDQ: EDX = sext(remainder) */
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiHi, kEdx);  /* HI.hi = sext(remainder) */
    return cursor;
}
