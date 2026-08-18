#include <cstddef>
#include <cstdint>

#include "../arm_cpu.h"
#include "../arm_emit_services.h"
#include "../arm_mmu_state.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"
#include "../../../cpu/arm_processor_config.h"

/* ARM DDI 0406C.c B9.3.1 CPS (Thumb) (p. B9-1978): "changes one or more of the
   CPSR.{A, I, F} interrupt mask bits and the CPSR.M mode field, without
   changing the other CPSR bits", and "CPS is treated as NOP if executed in
   User mode". */
uint8_t* PlaceCps(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    const ArmProcessorConfig* config = ctx->emit->ProcessorConfig();

    /* CPSRWriteByInstr (p. B1-1153): SCTLR.NMFI gates setting F. */
    if (config->HasCp15V7()) {
        EmitPushBaseDisp32(cursor, kMmuReg,
            static_cast<int32_t>(
                offsetof(ArmMmuState, effective_control_register)));
    } else {
        EmitPush32(cursor, 0u);
    }
    EmitPush32(cursor, 0u);
    EmitPush32(cursor, d->op1);
    EmitPush32(cursor, 0u);
    EmitPush32(cursor, d->immediate);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmCpu::WriteCpsrByInstrHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}
