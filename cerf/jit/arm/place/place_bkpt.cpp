#include "../arm_cpu.h"
#include "../arm_emit_services.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

/* ARM DDI 0406C.c A8.8.24 (p. A8-346): BKPT generates a debug event. The
   modified CERF 6.6 model routes that event through the prefetch-abort vector. */
uint8_t* PlaceBkpt(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmCpu::RaiseAbortPrefetchExceptionHelper));
    EmitAddRegImm32(cursor, kEsp, 8);
    EmitRet(cursor);
    return cursor;
}
