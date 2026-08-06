#include <cstdint>

#include "../../../core/log.h"
#include "../place_fns.h"
#include "../../x86_emit.h"

namespace {

[[noreturn]] void ArmUnimplementedFatalHelper(uint32_t pc, uint32_t opcode) {
    LOG(Jit, "FATAL: unimplemented ARM instruction 0x%08X executed at guest "
             "pc=0x%08X. Implement its decode/place path before lifting "
             "this.\n",
        opcode, pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

}

uint8_t* PlaceArmUnimplemented(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext*) {
    using namespace x86;
    EmitPush32(cursor, d->immediate);
    EmitPush32(cursor, d->guest_address);
    EmitCall(cursor, reinterpret_cast<void*>(&ArmUnimplementedFatalHelper));
    return cursor;
}
