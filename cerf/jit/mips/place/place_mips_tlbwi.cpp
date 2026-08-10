#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_cp0_ops.h"
#include "../mips_emit_services.h"
#include "../../x86_emit.h"

/* TLBWI: write the TLB entry indexed by CP0_Index from EntryHi / EntryLo0 /
   EntryLo1 / PageMask. No operands; TlbwiHelper drives MipsMmu::WriteIndexed. */
uint8_t* PlaceMipsTlbwi(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Cp0Ops())));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsCp0Ops::TlbwiHelper));
    return cursor;
}
