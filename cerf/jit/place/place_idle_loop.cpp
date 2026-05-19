#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../arm_jit.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceIdleLoop(uint8_t*      cursor,
                       DecodedInsn*  d,
                       BlockContext* ctx) {
    using namespace x86;

    /* PUSH INFINITE; PUSH idle_event_; CALL WaitForSingleObject;
       then tail-call PlaceBranch (the original branch-to-self
       loops back, so PlaceBranch emits the unconditional branch
       target).  */
    EmitPush32(cursor, 0xFFFFFFFFu);  /* INFINITE = (DWORD)-1 */
    EmitPush32(cursor,
               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit->IdleEvent())));
    EmitCall(cursor, reinterpret_cast<void*>(&WaitForSingleObject));
    return PlaceBranch(cursor, d, ctx);
}
