#include <cstdint>

#include "../../../core/fatal.h"
#include "../arm_emit_services.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

namespace {

[[noreturn]] void ArmUnimplementedFatalHelper(Fatal*   fatal,
                                              uint32_t pc,
                                              uint32_t opcode,
                                              uint32_t thumb) {
    fatal->Die("unimplemented %s instruction 0x%08X executed at guest "
               "pc=0x%08X\n",
               thumb != 0u ? "Thumb" : "ARM", opcode, pc);
}

}

uint8_t* PlaceArmUnimplemented(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) {
    using namespace x86;
    EmitPush32(cursor, ctx->thumb ? 1u : 0u);
    EmitPush32(cursor, d->immediate);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->FatalService())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmUnimplementedFatalHelper));
    return cursor;
}
