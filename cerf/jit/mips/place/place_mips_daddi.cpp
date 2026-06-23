#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* DADDI rt, rs, imm16 : rt = rs + sext64(imm16), 64-bit; a signed 64-bit overflow
   raises Integer Overflow and writes nothing (QEMU gen_arith_imm OPC_DADDI). The
   low ADD uses the EAX-form (0x05 id) so CF is always set - EmitAddRegImm32 would
   emit INC for imm==1 which does NOT set CF (Intel SDM Vol.2), breaking the ADC. */
uint8_t* PlaceMipsDaddi(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t imm_lo = static_cast<uint32_t>(static_cast<int32_t>(
                                static_cast<int16_t>(d->imm)));
    const uint32_t imm_hi = (d->imm & 0x8000u) ? 0xFFFFFFFFu : 0x00000000u;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    Emit8(cursor, 0x05);                       /* ADD eax, imm_lo (sets CF) */
    Emit32(cursor, imm_lo);
    EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, mips_emit::GprHiOff(d->rs));
    Emit8(cursor, 0x81);                       /* ADC edx, imm_hi (81 /2 id) */
    EmitModRmReg(cursor, 3, kEdx, 2);
    Emit32(cursor, imm_hi);
    mips_emit::EmitTrappingArith64Tail(cursor, d->rt, ctx->jit,
        reinterpret_cast<void*>(&MipsJit::ArithOverflowHelper), d->guest_address);
    return cursor;
}
