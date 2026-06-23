#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include "../mips_cpu_state.h"
#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

/* MULTU rs, rt : 64-bit unsigned product of rs[31:0] * rt[31:0]; LO = sext32 of
   the low word, HI = sext32 of the high word (QEMU translate.c gen_muldiv
   OPC_MULTU :3317: mulu2_i32 then ext_i32_tl into LO/HI). No GPR dest. */
uint8_t* PlaceMipsMultu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t kLoLo = static_cast<int32_t>(offsetof(MipsCpuState, lo));
    const int32_t kLoHi = kLoLo + 4;
    const int32_t kHiLo = static_cast<int32_t>(offsetof(MipsCpuState, hi));
    const int32_t kHiHi = kHiLo + 4;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rt));  /* ECX = rt.lo */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));  /* EAX = rs.lo */
    Emit8(cursor, 0xF7);                            /* MUL ecx (F7 /4): EDX:EAX = EAX*ECX */
    EmitModRmReg(cursor, 3, kEcx, 4);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoLo, kEax);  /* LO.lo = low product */
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiLo, kEdx);  /* HI.lo = high product */
    Emit8(cursor, 0x99);                            /* CDQ: EDX = sext(EAX=low product) */
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoHi, kEdx);  /* LO.hi = sext(low product) */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kHiLo);  /* EAX = high product */
    Emit8(cursor, 0x99);                            /* CDQ: EDX = sext(high product) */
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiHi, kEdx);  /* HI.hi = sext(high product) */
    return cursor;
}
