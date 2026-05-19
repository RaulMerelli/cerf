#include <cstddef>

#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceMSRImmediate(uint8_t*      cursor,
                           DecodedInsn*  d,
                           BlockContext* ctx) {
    using namespace x86;

    /* d->rn carries the field-mask, not a register number. */
    const uint32_t field_mask = ArmCpu::ComputePSRMaskValue(static_cast<int>(d->rn));
    if (field_mask == 0) {
        /* Empty field-mask MSR is a no-op; some encodings are pure
           data masquerading as MSR. */
        return cursor;
    }

    const uint32_t cpu_imm =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit->Cpu()));

    if (d->op1 & 2u) {
        /* MSR SPSR, imm — call UpdatePSRMaskHelper(current, new, mask, cpu)
           and store result to SPSR. */
        EmitPush32(cursor, cpu_imm);
        EmitPush32(cursor, field_mask);
        EmitPush32(cursor, d->immediate);
        EmitPushBaseDisp32(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, spsr)));
        EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::UpdatePSRMaskHelper));
        EmitAddRegImm32(cursor, kEsp, 16);
        EmitMovBaseDisp32Reg(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, spsr)), kEax);
    } else {
        /* MSR CPSR, imm */
        if (d->rn == 8) {
            /* Flags-only fast path — call UpdateFlagsHelper(cpu, imm). */
            EmitPush32(cursor, d->immediate);
            EmitPush32(cursor, cpu_imm);
            EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::UpdateFlagsHelper));
            EmitAddRegImm32(cursor, kEsp, 8);
        } else {
            /* General path: get current CPSR-with-flags, mask-update,
               write back via UpdateCpsrWithFlagsHelper. */
            EmitPush32(cursor, cpu_imm);
            EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::GetCpsrWithFlagsHelper));
            EmitAddRegImm32(cursor, kEsp, 4);
            /* EAX = current CPSR-full. UpdatePSRMaskHelper(EAX, imm, mask, cpu). */
            EmitPush32(cursor, cpu_imm);
            EmitPush32(cursor, field_mask);
            EmitPush32(cursor, d->immediate);
            EmitPushReg(cursor, kEax);
            EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::UpdatePSRMaskHelper));
            EmitAddRegImm32(cursor, kEsp, 16);
            /* EAX = updated CPSR-full. UpdateCpsrWithFlagsHelper(cpu, EAX). */
            EmitPushReg(cursor, kEax);
            EmitPush32(cursor, cpu_imm);
            EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::UpdateCpsrWithFlagsHelper));
            EmitAddRegImm32(cursor, kEsp, 8);
        }
    }
    return cursor;
}
