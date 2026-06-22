#pragma once

#include <cstdint>

#include "mips_decoded_insn.h"

class MipsJit;

constexpr uint32_t kMaxMipsInsnPerBlock = 100;

/* Per-block emit state passed to every MIPS place_fn. Trampoline-target fields
   are added as the engine's trampolines are designed; offsets are not baked
   into emitted code until the engine emits, so extending this is free until
   then. */
struct MipsBlockContext {
    MipsJit*        jit;

    MipsDecodedInsn insns[kMaxMipsInsnPerBlock];
    uint32_t        num_insns;

    /* Physical page base (PA & ~0xFFF) of insns[0], captured from its fetch -
       the block's phys identity (blocks are 4 KiB-page-bounded). */
    uint32_t        block_phys_page_base;
};
