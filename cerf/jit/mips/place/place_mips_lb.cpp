#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit_alu.h"

/* LB rt, offset(rs): rt = sext64(sext8(mem[gpr[rs] + sext(imm16)])). The load
   runs even when rt==0 (its translate/fault side effects are architectural);
   only the register write is skipped. */
uint8_t* PlaceMipsLb(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t sext = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEcx, sext);
    EmitMovRegImm32(cursor, kEdx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::LoadByteHelper));
    if (d->rt != 0) {
        EmitMovsxReg32Reg8(cursor, kEax, kAl);
        mips_emit::EmitStoreGprSextEax(cursor, d->rt);
    }
    return cursor;
}
