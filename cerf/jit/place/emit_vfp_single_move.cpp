#include <cstddef>
#include <cstdint>

#include "../arm_cpu.h"
#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../x86_emit.h"

/* VMOV Sn <-> Rt — single 32-bit transfer between ARM core register
   and VFP single-precision register. Encoding form cp_num=10,
   cp_opc=0, op2=0, CRm=0; L bit selects direction. Per
   references/omap3530/armv7_arch_excerpts.txt § VMOV Sn <-> Rt. */

uint8_t* EmitVfpSingleMove(uint8_t*      cursor,
                           DecodedInsn*  d,
                           BlockContext* ctx) {
    using namespace x86;
    ArmJit* jit = ctx->jit;

    const uint32_t sn = (((d->cp >> 2) & 1u) << 4) | (d->crn & 0xFu);
    const int32_t sn_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, vfp_d) + sn * 4u);
    const int32_t rt_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);

    if (d->l) {
        if (d->rd == 15) {
            /* Rt=R15 — FPSCR-style NZCV transfer to APSR. */
            EmitPushBaseDisp32(cursor, kStateReg, sn_disp);
            EmitPush32(cursor,
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(jit->Cpu())));
            EmitCall(cursor, reinterpret_cast<void*>(
                &ArmCpu::UpdateNzcvOnlyHelper));
            EmitAddRegImm32(cursor, kEsp, 8);
            return cursor;
        }
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, sn_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, rt_disp, kEax);
        return cursor;
    }

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rt_disp);
    EmitMovBaseDisp32Reg(cursor, kStateReg, sn_disp, kEax);
    return cursor;
}
