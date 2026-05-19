#include <cstddef>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceBasicTwoAddrWithResult(uint8_t*      cursor,
                                     uint8_t       arith_reg_opcode,
                                     uint8_t       arith_imm32_opcode,
                                     uint8_t       arith_imm32_reg,
                                     DecodedInsn*  d,
                                     BlockContext* ctx,
                                     uint8_t       immediate_reg) {
    using namespace x86;
    if (d->rn == d->rd && d->rn != ArmGpr::kR15) {
        if (d->i) {
            /* OP r/m32, imm32 — opcode + ModRM(mod=10 r/m=ESI reg=arith_imm32_reg) + disp32 + imm32. */
            Emit8(cursor, arith_imm32_opcode);
            EmitModRmReg(cursor, 2, kStateReg, arith_imm32_reg);
            Emit32(cursor,
                static_cast<uint32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4));
            Emit32(cursor, d->reserved3);
        } else {
            /* OP r/m32, reg. */
            Emit8(cursor, arith_reg_opcode);
            EmitModRmReg(cursor, 2, kStateReg, immediate_reg);
            Emit32(cursor,
                static_cast<uint32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4));
        }
    } else {
        if (d->rn == ArmGpr::kR15) {
            EmitMovRegImm32(cursor, kEax,
                d->guest_address +
                (ctx->jit->CpuState()->cpsr.bits.thumb_mode ? 4u : 8u));
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rn * 4));
        }
        if (d->i) {
            /* Skip-no-op heuristic: if Imm32 == 0 and the
               opcode is ADD/SUB without S, drop the instruction. */
            if (d->s == 0 && d->reserved3 == 0u &&
                arith_imm32_opcode == 0x81 &&
                (arith_imm32_reg == 0 || arith_imm32_reg == 5)) {
                /* No-op. */
            } else {
                Emit8(cursor, arith_imm32_opcode);
                EmitModRmReg(cursor, 3, kEax, arith_imm32_reg);
                Emit32(cursor, d->reserved3);
            }
        } else {
            Emit8(cursor, arith_reg_opcode);
            EmitModRmReg(cursor, 3, kEax, immediate_reg);
        }
        EmitMovBaseDisp32Reg(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4),
            kEax);
    }
    return cursor;
}
