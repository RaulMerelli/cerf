#include "mips_jit.h"

#include <cstdint>

/* Doubleword variable-shift helpers (DSLLV/DSRLV/DSRAV). A 64-bit shift by a
   runtime count in [0,63] has no single x86-32 instruction (the >=32 case
   crosses the word boundary), so the JIT-emitted place fns CALL these. Count is
   gpr[rs] & 0x3f (QEMU gen_shift OPC_DSLLV/DSRLV/DSRAV). */

void __fastcall MipsJit::DsllvHelper(uint32_t rd, uint32_t rt, uint32_t rs, MipsJit* jit) {
    if (rd == 0) {
        return;
    }
    const uint32_t n = static_cast<uint32_t>(jit->cpu_state_.gpr[rs]) & 0x3fu;
    jit->cpu_state_.gpr[rd] = jit->cpu_state_.gpr[rt] << n;
}

void __fastcall MipsJit::DsrlvHelper(uint32_t rd, uint32_t rt, uint32_t rs, MipsJit* jit) {
    if (rd == 0) {
        return;
    }
    const uint32_t n = static_cast<uint32_t>(jit->cpu_state_.gpr[rs]) & 0x3fu;
    jit->cpu_state_.gpr[rd] = jit->cpu_state_.gpr[rt] >> n;   /* logical (uint64) */
}

void __fastcall MipsJit::DsravHelper(uint32_t rd, uint32_t rt, uint32_t rs, MipsJit* jit) {
    if (rd == 0) {
        return;
    }
    const uint32_t n = static_cast<uint32_t>(jit->cpu_state_.gpr[rs]) & 0x3fu;
    jit->cpu_state_.gpr[rd] = static_cast<uint64_t>(
        static_cast<int64_t>(jit->cpu_state_.gpr[rt]) >> n);   /* arithmetic */
}
