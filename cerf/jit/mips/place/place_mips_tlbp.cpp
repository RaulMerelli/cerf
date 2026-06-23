#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* TLBP: probe the TLB for the entry matching EntryHi, writing its index (or the
   P bit on a miss) to CP0_Index. No operands; TlbpHelper drives MipsMmu::Probe. */
uint8_t* PlaceMipsTlbp(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::TlbpHelper));
    return cursor;
}
