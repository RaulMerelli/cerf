#include <cstddef>
#include <cstdint>

#include "../arm_cpu.h"
#include "../arm_jit.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../../cpu/arm_processor_config.h"

uint8_t* PlaceMRSorMSR(uint8_t*      cursor,
                       DecodedInsn*  d,
                       BlockContext* ctx) {
    using namespace x86;
    const ArmProcessorConfig* config = ctx->jit->ProcessorConfig();

    if (d->s == 0u) {
        /* MRS. Rd == 15 is UNPREDICTABLE (ARM DDI 0406C.c A8.8.109,
           p. A8-496). */
        if (d->rd == 15u) {
            return EmitRaiseUndAndReturn(cursor, d, ctx);
        }
        if (d->n != 0u) {
            cursor = EmitSpsrModeGuard(cursor, d, ctx);
            EmitPush32(cursor,
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(ctx->jit->Cpu())));
            EmitCall(cursor,
                reinterpret_cast<void*>(&ArmCpu::ReadSpsrHelper));
            EmitAddRegImm32(cursor, kEsp, 4);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, cpsr)));
            if (config->HasCp15V7()) {
                /* ARM DDI 0406C.c B9.3.8 MRS (p. B9-1991): "CPSR is read
                   with execution state bits other than E masked out",
                   AND '11111000 11111111 00000011 11011111'. */
                EmitAndRegImm32(cursor, kEax, 0xF8FF03DFu);
            }
        }
        EmitMovBaseDisp32Reg(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u),
            kEax);
        return cursor;
    }

    /* MSR (register). Rn == 15 is UNPREDICTABLE (ARM DDI 0406C.c B9.3.12,
       p. B9-1998, encoding A1 ARMv4 on). mask == 0 is UNPREDICTABLE on
       ARMv7 only - DDI 0100I A4.1.39 (p. A4-77) merges byte_mask = 0 as
       a PSR-unchanged no-op on the earlier versions. */
    if (d->rm == 15u || (d->crn == 0u && config->HasCp15V7())) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rm * 4u));
    return EmitMsrWriteTail(cursor, d, ctx);
}
