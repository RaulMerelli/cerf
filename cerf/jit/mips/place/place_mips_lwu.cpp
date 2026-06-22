#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* LWU rt, offset(rs): rt = zero_extend64(mem[gpr[rs] + sext(imm16)][31:0]).
   (QEMU translate.c OPC_LWU: MO_UL.) LoadWordHelper returns the word in EAX;
   LWU zero-extends it (rt.hi = 0). The load runs even when rt==0 (translate /
   fault / MMIO side effects are architectural); only the write is skipped. */
uint8_t* PlaceMipsLwu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t sext = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEcx, sext);                                /* ECX = EA */
    EmitMovRegImm32(cursor, kEdx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::LoadWordHelper)); /* EAX = word */
    if (d->rt != 0) {
        EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rt), kEax);
        EmitMovBaseDisp32Imm32(cursor, kStateReg, mips_emit::GprHiOff(d->rt), 0);
    }
    return cursor;
}
