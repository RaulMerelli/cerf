#include "../arm_jit.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceRaiseUndefinedException(uint8_t*      cursor,
                                      DecodedInsn*  d,
                                      BlockContext* ctx) {
    using namespace x86;

    /* PUSH guest_addr, PUSH cpu, CALL helper, ADD ESP, 8.
       Helper returns the native PC of the UND vector or nullptr.
       TEST + JZ to fall-through that RETs to dispatcher; otherwise
       JMP EAX into the vector block. */
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit->Cpu())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::RaiseUndefinedExceptionHelper));
    EmitAddRegImm32(cursor, kEsp, 8);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* not_in_cache = EmitJzLabel(cursor);
    EmitJmpReg(cursor, kEax);
    FixupLabel(not_in_cache, cursor);
    EmitRetn(cursor, 0);
    return cursor;
}
