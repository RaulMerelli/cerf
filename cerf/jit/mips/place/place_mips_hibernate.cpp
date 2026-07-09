#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* HIBERNATE: freeze the pipeline until a Cold Reset (UM ch.27 p587). HibernateHelper
   sets pc past the instruction and halts the CPU; ends_block makes codegen emit the
   ret, so Run() returns and JitRunner::RunLoop parks. */
uint8_t* PlaceMipsHibernate(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx, d->guest_address + 4u);
    EmitMovRegImm32(cursor, kEdx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::HibernateHelper));
    return cursor;
}
