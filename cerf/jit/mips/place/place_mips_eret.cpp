#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_cp0_ops.h"
#include "../mips_emit_services.h"
#include "../../x86_emit.h"

uint8_t* PlaceMipsEret(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Cp0Ops())));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsCp0Ops::EretHelper));
    return cursor;
}
