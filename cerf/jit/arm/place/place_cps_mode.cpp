#include <cstdint>

#include "../arm_cpu.h"
#include "../arm_emit_services.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

/* ARM DDI 0406C.c B9.3.2 CPS (p. B9-1979). */
uint8_t* PlaceCpsMode(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    EmitPush32(cursor, d->immediate);
    EmitPush32(cursor, d->rn);
    EmitPush32(cursor, d->op1);
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                            ctx->emit->Cpu())));
    EmitCall(cursor,
             reinterpret_cast<void*>(&ArmCpu::ChangeProcessorStateHelper));
    EmitAddRegImm32(cursor, kEsp, 16u);
    return cursor;
}
