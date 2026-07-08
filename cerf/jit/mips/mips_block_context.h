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

    /* Physical page base (PA & ~page_off_mask) of insns[0], captured from its
       fetch - the block's phys identity. Blocks are bounded to the SoC minimum
       page (min_page_shift): 4 KiB on VR5500, 1 KiB on VR4102. */
    uint32_t        block_phys_page_base;
};
