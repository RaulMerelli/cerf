#include <cstddef>
#include <cstdint>

#include "../../../cpu/arm_processor_config.h"
#include "../arm_cpu.h"
#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

/* VMRS / VMSR - VFP system-register move. Encoding: cp_num=10,
   cp_opc=7, CRm=0, opc2=0; CRn selects which VFP system register.
   Per references/omap3530/armv7_arch_excerpts.txt § VFP system
   registers - VMRS / VMSR. */

uint8_t* EmitVfpSystemRegTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) {
    using namespace x86;
    ArmJit* jit = ctx->jit;
    const int32_t rd_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);

    /* CRn values per QEMU cpu.h ARM_VFP_* constants (excerpt §VFP). */
    constexpr uint32_t kFpsid  = 0;
    constexpr uint32_t kFpscr  = 1;
    constexpr uint32_t kMvfr1  = 6;
    constexpr uint32_t kMvfr0  = 7;
    constexpr uint32_t kFpexc  = 8;

    if (d->l) {
        /* VMRS - read VFP system register → Rt. */
        switch (d->crn) {
        case kFpsid:
            if (d->rd == 15) {
                return EmitRaiseUndAndReturn(cursor, d, ctx);
            }
            EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                jit->ProcessorConfig()->Fpsid());
            return cursor;

        case kFpscr:
            if (d->rd == 15) {
                /* __cdecl: PUSH args RTL, CALL, ADD ESP. */
                EmitPushBaseDisp32(cursor, kStateReg,
                    static_cast<int32_t>(offsetof(ArmCpuState, fpscr)));
                EmitPush32(cursor,
                    static_cast<uint32_t>(
                        reinterpret_cast<uintptr_t>(jit->Cpu())));
                EmitCall(cursor, reinterpret_cast<void*>(
                    &ArmCpu::UpdateNzcvOnlyHelper));
                EmitAddRegImm32(cursor, kEsp, 8);
                return cursor;
            }
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, fpscr)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            return cursor;

        case kMvfr1:
            if (d->rd == 15) {
                return EmitRaiseUndAndReturn(cursor, d, ctx);
            }
            EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                jit->ProcessorConfig()->Mvfr1());
            return cursor;

        case kMvfr0:
            if (d->rd == 15) {
                return EmitRaiseUndAndReturn(cursor, d, ctx);
            }
            EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                jit->ProcessorConfig()->Mvfr0());
            return cursor;

        case kFpexc:
            if (d->rd == 15) {
                return EmitRaiseUndAndReturn(cursor, d, ctx);
            }
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, fpexc)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            return cursor;
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* VMSR - write Rt → VFP system register. */
    switch (d->crn) {
    case kFpsid:
    case kMvfr1:
    case kMvfr0:
        /* Writes ignored on read-only system registers (excerpt
           §VFP, QEMU translate-vfp.c:1467-1473). */
        return cursor;

    case kFpscr:
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, fpscr)), kEax);
        return cursor;

    case kFpexc:
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, fpexc)), kEax);
        return cursor;
    }
    return EmitRaiseUndAndReturn(cursor, d, ctx);
}
