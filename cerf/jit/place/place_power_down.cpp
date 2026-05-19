#include "../arm_jit.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlacePowerDown(uint8_t*      cursor,
                        DecodedInsn*  /* d */,
                        BlockContext* /* ctx */) {
    using namespace x86;
    EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::PowerDownHelper));
    /* Helper does not return (CerfFatalExit). The trailing PUSH 0;
       CALL exit is unreachable here — omit. */
    return cursor;
}
