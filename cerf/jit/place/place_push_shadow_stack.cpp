#include <cstring>

#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlacePushShadowStack(uint8_t*      cursor,
                              DecodedInsn*  /* d */,
                              BlockContext* ctx) {
    using namespace x86;

    /* Emit MOV EDI, <cache slot ptr placeholder>; CALL helper;
       JMP forward over inline cache slot; align to 4-byte boundary;
       store cursor address into the placeholder; emit 4 zero bytes
       for the cache slot; resume after JMP. */
    EmitMovRegImm32(cursor, kEdi, 0);
    uint8_t* cache_ptr_imm_location = cursor - 4;

    EmitCall(cursor, ctx->shadow_stack_helper_target);

    uint8_t* skip_cache = EmitJmpLabel(cursor);

    /* Align cache slot to 4-byte boundary. */
    cursor = reinterpret_cast<uint8_t*>(
        (reinterpret_cast<uintptr_t>(cursor) + 3u) & ~uintptr_t{3});

    /* Patch the MOV EDI imm32 with the cache slot address. */
    const uint32_t slot_addr =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cursor));
    std::memcpy(cache_ptr_imm_location, &slot_addr, 4);

    /* Emit the 4-byte cache slot (initially 0 → unresolved). */
    Emit32(cursor, 0);

    FixupLabel(skip_cache, cursor);
    return cursor;
}
