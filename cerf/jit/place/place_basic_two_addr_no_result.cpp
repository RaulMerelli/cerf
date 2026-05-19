#include <cstddef>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceBasicTwoAddrNoResult(uint8_t*      cursor,
                                   uint8_t       arith_reg_opcode,
                                   uint8_t       arith_imm32_opcode,
                                   uint8_t       arith_imm32_reg,
                                   DecodedInsn*  d,
                                   BlockContext* ctx,
                                   uint8_t       immediate_reg,
                                   bool          fOpcodeHasSideEffect) {
    using namespace x86;
    if (d->rn == ArmGpr::kR15 || fOpcodeHasSideEffect) {
        if (d->rn == ArmGpr::kR15) {
            EmitMovRegImm32(cursor, kEax,
                d->guest_address +
                (ctx->jit->CpuState()->cpsr.bits.thumb_mode ? 4u : 8u));
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rn * 4));
        }
        if (d->i) {
            Emit8(cursor, arith_imm32_opcode);
            EmitModRmReg(cursor, 3, kEax, arith_imm32_reg);
            Emit32(cursor, d->reserved3);
        } else {
            Emit8(cursor, arith_reg_opcode);
            EmitModRmReg(cursor, 3, kEax, immediate_reg);
        }
    } else {
        if (d->i) {
            Emit8(cursor, arith_imm32_opcode);
            EmitModRmReg(cursor, 2, kStateReg, arith_imm32_reg);
            Emit32(cursor,
                static_cast<uint32_t>(offsetof(ArmCpuState, gprs) + d->rn * 4));
            Emit32(cursor, d->reserved3);
        } else {
            Emit8(cursor, arith_reg_opcode);
            EmitModRmReg(cursor, 2, kStateReg, immediate_reg);
            Emit32(cursor,
                static_cast<uint32_t>(offsetof(ArmCpuState, gprs) + d->rn * 4));
        }
    }
    return cursor;
}
