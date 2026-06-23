#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* TLBR: read the TLB entry indexed by CP0_Index into EntryHi/EntryLo0/EntryLo1/
   PageMask. No operands; TlbrHelper drives MipsMmu::Read. */
uint8_t* PlaceMipsTlbr(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::TlbrHelper));
    return cursor;
}
