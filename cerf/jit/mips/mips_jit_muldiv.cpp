#include "mips_jit.h"

#include <cstdint>

/* 64-bit (doubleword) multiply/divide helpers that exceed practical inline emit
   on the 32-bit x86 host. The JIT-emitted place fns CALL these. */

namespace {

/* QEMU host-utils.h mulu64 (the non-__int128 fallback): 64x64 -> 128-bit unsigned
   product via 32-bit limbs. x86-32 has no __int128/_umul128. */
void Mulu64(uint64_t a, uint64_t b, uint64_t* lo, uint64_t* hi) {
    const uint32_t a0 = static_cast<uint32_t>(a), a1 = static_cast<uint32_t>(a >> 32);
    const uint32_t b0 = static_cast<uint32_t>(b), b1 = static_cast<uint32_t>(b >> 32);

    const uint64_t rl = static_cast<uint64_t>(a0) * b0;
    const uint64_t rm = static_cast<uint64_t>(a0) * b1;
    const uint64_t rn = static_cast<uint64_t>(a1) * b0;
    const uint64_t rh = static_cast<uint64_t>(a1) * b1;

    uint64_t c = static_cast<uint32_t>(rl >> 32) +
                 static_cast<uint32_t>(rm) + static_cast<uint32_t>(rn);
    const uint32_t lo_hi = static_cast<uint32_t>(c);
    c >>= 32;
    c += static_cast<uint32_t>(rm >> 32) + static_cast<uint32_t>(rn >> 32) +
         static_cast<uint32_t>(rh);
    const uint32_t hi_lo = static_cast<uint32_t>(c);
    const uint32_t hi_hi = static_cast<uint32_t>(rh >> 32) + static_cast<uint32_t>(c >> 32);

    *lo = (static_cast<uint64_t>(lo_hi) << 32) | static_cast<uint32_t>(rl);
    *hi = (static_cast<uint64_t>(hi_hi) << 32) | hi_lo;
}

}  // namespace

void __fastcall MipsJit::DmultuHelper(uint32_t rs, uint32_t rt, MipsJit* jit) {
    /* {HI,LO} = gpr[rs] * gpr[rt], full 128-bit UNSIGNED (QEMU gen_muldiv
       OPC_DMULTU -> mulu2_i64). */
    Mulu64(jit->cpu_state_.gpr[rs], jit->cpu_state_.gpr[rt],
           &jit->cpu_state_.lo, &jit->cpu_state_.hi);
}

void __fastcall MipsJit::DmultHelper(uint32_t rs, uint32_t rt, MipsJit* jit) {
    /* {HI,LO} = gpr[rs] * gpr[rt], full 128-bit SIGNED = unsigned product with the
       sign correction (QEMU host-utils.h muls64; gen_muldiv OPC_DMULT muls2_i64). */
    const uint64_t a = jit->cpu_state_.gpr[rs];
    const uint64_t b = jit->cpu_state_.gpr[rt];
    uint64_t lo, hi;
    Mulu64(a, b, &lo, &hi);
    if (static_cast<int64_t>(a) < 0) hi -= b;
    if (static_cast<int64_t>(b) < 0) hi -= a;
    jit->cpu_state_.lo = lo;
    jit->cpu_state_.hi = hi;
}

void __fastcall MipsJit::DdivHelper(uint32_t rs, uint32_t rt, MipsJit* jit) {
    /* LO=quot, HI=rem, signed 64-bit. div-by-zero and INT64_MIN/-1 substitute
       divisor=1 to avoid the host #DE (QEMU gen_muldiv OPC_DDIV movcond); the
       MIPS result for those cases is architecturally UNPREDICTABLE. */
    const int64_t a = static_cast<int64_t>(jit->cpu_state_.gpr[rs]);
    int64_t b = static_cast<int64_t>(jit->cpu_state_.gpr[rt]);
    if (b == 0 || (a == static_cast<int64_t>(0x8000000000000000ULL) && b == -1)) {
        b = 1;
    }
    jit->cpu_state_.lo = static_cast<uint64_t>(a / b);
    jit->cpu_state_.hi = static_cast<uint64_t>(a % b);
}

void __fastcall MipsJit::DdivuHelper(uint32_t rs, uint32_t rt, MipsJit* jit) {
    /* LO=quot, HI=rem, unsigned 64-bit. div-by-zero substitutes divisor=1 (QEMU
       gen_muldiv OPC_DDIVU movcond). */
    const uint64_t a = jit->cpu_state_.gpr[rs];
    uint64_t b = jit->cpu_state_.gpr[rt];
    if (b == 0) {
        b = 1;
    }
    jit->cpu_state_.lo = a / b;
    jit->cpu_state_.hi = a % b;
}
