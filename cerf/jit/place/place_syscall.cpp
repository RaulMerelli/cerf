#include "../arm_jit.h"
#include "../place_fns.h"
#include "../x86_emit.h"

uint8_t* PlaceSyscall(uint8_t*      cursor,
                      DecodedInsn*  /* d */,
                      BlockContext* /* ctx */) {
    using namespace x86;
    EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::PerformSyscallHelper));
    return cursor;
}
