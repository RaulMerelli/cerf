#include <cstdint>

#include "../arm_cpu.h"
#include "../arm_jit.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* EmitRaiseUndAndReturn(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    ArmJit* jit = ctx->jit;
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(jit->Cpu())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmCpu::RaiseUndefinedExceptionHelper));
    EmitAddRegImm32(cursor, kEsp, 8);
    EmitRetn(cursor, 0);
    return cursor;
}
