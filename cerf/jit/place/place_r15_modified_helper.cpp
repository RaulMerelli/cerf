#include <cstring>

#include "../arm_jit.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceR15ModifiedHelper(uint8_t*      cursor,
                                DecodedInsn*  d,
                                BlockContext* ctx) {
    using namespace x86;

    EmitMovRegImm32(cursor, kEdx, d->guest_address);

    EmitMovRegImm32(cursor, kEdi, 0);
    uint8_t* cache_ptr_imm_location = cursor - 4;

    EmitJmp32(cursor, ctx->r15_modified_helper_target);

    /* Align cache pair to 4-byte boundary. */
    cursor = reinterpret_cast<uint8_t*>(
        (reinterpret_cast<uintptr_t>(cursor) + 3u) & ~uintptr_t{3});

    const uint32_t cache_addr =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cursor));
    std::memcpy(cache_ptr_imm_location, &cache_addr, 4);

    /* GuestLast = 0 */
    Emit32(cursor, 0);
    /* NativeLast = &NotJittedHelper (CERF re-uses NotJittedHelper
       for the cache-pair init role; both are 1-byte RETN thunks that exit JIT to dispatcher). */
    EmitPtr(cursor, reinterpret_cast<void*>(&ArmJit::NotJittedHelper));

    return cursor;
}
