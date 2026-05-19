#include "../arm_jit.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceSoftwareInterrupt(uint8_t*      cursor,
                                DecodedInsn*  d,
                                BlockContext* ctx) {
    using namespace x86;
    /* Same dispatch shape as PlaceRaiseUndefinedException: PUSH
       guest_addr, PUSH cpu, CALL helper, ADD ESP 8, TEST EAX,EAX,
       JZ NotInCache, JMP EAX, NotInCache: RETN. */
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit->Cpu())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::RaiseSoftwareInterruptExceptionHelper));
    EmitAddRegImm32(cursor, kEsp, 8);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* not_in_cache = EmitJzLabel(cursor);
    EmitJmpReg(cursor, kEax);
    FixupLabel(not_in_cache, cursor);
    EmitRetn(cursor, 0);
    return cursor;
}
