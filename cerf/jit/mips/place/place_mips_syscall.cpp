#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_jit.h"
#include "../../x86_emit.h"

/* SYSCALL: raise the Sys CP0 exception via SyscallHelper (QEMU OPC_SYSCALL ->
   EXCP_SYSCALL -> cause 8). No operands. */
uint8_t* PlaceMipsSyscall(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsJit::SyscallHelper));
    return cursor;
}
