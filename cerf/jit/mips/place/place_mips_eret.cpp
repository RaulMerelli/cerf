#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* ERET: return from exception. No operands; EretHelper restores PC from EPC /
   ErrorEPC and clears EXL / ERL / LLbit. The block ends here (ERET has no delay
   slot); JitGenerateCode emits the ret after this and suppresses the
   straight-line pc override so EretHelper's pc stands. */
uint8_t* PlaceMipsEret(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::EretHelper));
    return cursor;
}
