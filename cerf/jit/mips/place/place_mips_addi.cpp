#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* ADDI rt, rs, imm : rt = sext32(rs[31:0] + sext(imm16)); a signed 32-bit
   overflow raises Integer Overflow and writes nothing. */
uint8_t* PlaceMipsAddi(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t sext = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));

    /* imm==0 cannot overflow and emits no flag-setting add - re-sign-extend
       rs's low word into rt directly (the per-insn cycle-bump's OF is stale). */
    if (sext == 0) {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
        if (d->rt != 0) {
            mips_emit::EmitStoreGprSextEax(cursor, d->rt);
        }
        return cursor;
    }

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEax, sext);             /* sets OF on signed overflow */
    mips_emit::EmitTrappingArith32Tail(cursor, d->rt, ctx->jit,
        reinterpret_cast<void*>(&MipsJit::ArithOverflowHelper), d->guest_address);
    return cursor;
}
