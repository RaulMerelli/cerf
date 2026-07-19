#pragma once

#include <cstdint>

#include "mips_decoded_insn.h"

class MipsJit;
enum class MipsTlbResult;

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

    /* QEMU tb_link_page page_addr[1]: a 4-byte MIPS16 EXTEND/JAL "can cross a
       word boundary" (U15509EJ2V0UM Table 3-20 p83), hence the min page too.
       tail_split = byte offset where the second translation page begins (0 =
       single-page block); tail_page_pa captured at the tail halfword's fetch. */
    uint32_t        tail_split;
    uint32_t        tail_page_pa;

    uint32_t        fetch_fault_pending;
    uint32_t        fetch_fault_va;
    MipsTlbResult   fetch_fault_res;

    /* insns[0] is PC-relative (U15509EJ2V0UM Table 3-12 p67): decoded with its
       own PC as base, which is wrong if the block is entered as a pending
       jump's delay slot (base = the jump's PC) - codegen guards that entry
       with a loud fatal. */
    uint32_t        insn0_pcrel_guard;
};
