#include "mips_jit.h"

#include <cstring>

#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "mips_place_fns.h"

void* MipsJit::JitCompile(uint32_t guest_pc) {
    /* MIPS16 instructions sit at halfword boundaries ("the instruction is
       located at the halfword boundary", U15509EJ2V0UM Table 3-19 p82 JR). */
    const bool m16 = cpu_state_.isa_mode != 0u;
    IsaBlockSpace& space = m16 ? blocks16_ : blocks_;
    if (guest_pc & (m16 ? 0x1u : 0x3u)) {
        /* Unaligned instruction fetch -> AdEL (MIPS PC must be word-aligned). CE's
           PSL implicit-call trap relies on it: coredll jumps to an unaligned magic
           VA and the kernel decodes the fault EPC into the API method. */
        DeliverFetchAddressError(guest_pc);
        return nullptr;
    }

    uint32_t pa0 = 0;
    const MipsTlbResult fr =
        mmu_->Translate(&cpu_state_, guest_pc, MipsAccess::kFetch, &pa0);
    if (fr != MipsTlbResult::kMatch) {
        /* Instruction-fetch TLB miss/invalid: deliver TLBL so the guest's refill
           handler maps the page, then bail - Run re-dispatches at the vector. */
        DeliverFetchTlbException(guest_pc, fr);
        return nullptr;
    }
    /* Block phys-identity + decode span are bounded to the SoC minimum page
       (1<<min_page_shift): a block that crossed a min-page boundary would have a
       phys_start that only pins its first page, so an independent remap of a later
       page would go undetected (invisible at 4 KB / VR5500; real at 1 KB / VR4102). */
    const uint32_t page_off_mask = (1u << cpu_state_.min_page_shift) - 1u;
    block_ctx_.block_phys_page_base = pa0 & ~page_off_mask;
    const uint32_t phys_start = block_ctx_.block_phys_page_base | (guest_pc & page_off_mask);

    /* Global (kseg / G=1) blocks live in the shared `global` index; per-process
       (G=0) blocks in per_asid[ASID]. */
    const uint8_t asid = static_cast<uint8_t>(cpu_state_.cp0_entryhi & 0xFFu);
    const bool outer_global = mmu_->ExecPageGlobal(&cpu_state_, guest_pc);
    JitBlockIndex& idx = outer_global ? space.global : space.per_asid[asid];

    JitBlock* ex = space.per_asid[asid].FindExact(guest_pc);
    if (!ex) ex = space.global.FindExact(guest_pc);
    if (ex) {
        if (ex->native_start && ex->phys_start == phys_start &&
            TailStillMapped(ex)) {
            space.JumpCacheInsert(guest_pc, ex->native_start, ex);
            return ex->native_start;     /* phys-checked reuse */
        }
        space.RemoveExact(guest_pc);     /* stale phys at same VA - evict */
    }

    block_ctx_.tail_split          = 0;
    block_ctx_.tail_page_pa        = 0;
    block_ctx_.fetch_fault_pending = 0;
    block_ctx_.insn0_pcrel_guard   = 0;
    if (m16) JitDecode16(guest_pc); else JitDecode(guest_pc);
    if (block_ctx_.num_insns == 0) {
        if (block_ctx_.fetch_fault_pending) {
            DeliverFetchTlbException(block_ctx_.fetch_fault_va,
                                     block_ctx_.fetch_fault_res);
            return nullptr;
        }
        LOG(Caution, "MipsJit::JitCompile: decoded 0 insns at 0x%08X\n", guest_pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    /* One slab holds the outer entrypoint record + the emitted code. The
       per-insn upper bound (128 B) far exceeds any current place fn. */
    const size_t code_est  = static_cast<size_t>(block_ctx_.num_insns) * 128u + 64u;
    const size_t slab_size = JitBlockIndex::OuterEntrySize() + code_est;
    uint8_t* slab = arena_.Allocate(slab_size);
    if (!slab) {
        arena_.Flush();
        blocks_.FlushAll();
        blocks16_.FlushAll();
        slab = arena_.Allocate(slab_size);
        if (!slab) {
            LOG(Caution, "MipsJit::JitCompile: arena exhausted (%zu bytes)\n", slab_size);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }
    uint8_t* code = slab + JitBlockIndex::OuterEntrySize();

    const MipsDecodedInsn& last = block_ctx_.insns[block_ctx_.num_insns - 1];
    JitBlock nb{};
    nb.guest_start  = guest_pc;
    nb.guest_end    = last.guest_address + last.length - 1u;
    nb.phys_start   = phys_start;
    nb.native_start = code;
    JitBlock* stored = idx.PlaceOuterAt(slab, nb);
    space.IndexInsert(stored, &idx, BlockIndexKey(phys_start),
                      block_ctx_.tail_split,
                      block_ctx_.tail_split != 0u
                          ? BlockIndexKey(block_ctx_.tail_page_pa)
                          : kBlockUnindexed);
    if (!outer_global) space.MarkPopulated(asid);

    const size_t code_size = JitGenerateCode(code, 1);
    arena_.FreeUnusedTail(code + code_size);
    stored->native_end = code + code_size;

    space.JumpCacheInsert(guest_pc, stored->native_start, stored);
    return stored->native_start;
}

bool MipsJit::TailStillMapped(const JitBlock* blk) {
    if (blk->index_split == 0u) {
        return true;
    }
    uint32_t pa = 0;
    if (mmu_->Translate(&cpu_state_, blk->guest_start + blk->index_split,
                        MipsAccess::kFetch, &pa) != MipsTlbResult::kMatch) {
        return false;
    }
    return BlockIndexKey(pa) == blk->index_start2;
}

void MipsJit::JitDecode(uint32_t guest_pc) {
    guest_pc &= ~0x3u;
    const uint32_t page_off_mask = (1u << cpu_state_.min_page_shift) - 1u;
    const uint32_t page_end = (guest_pc & ~page_off_mask) + page_off_mask + 1u;
    std::memset(block_ctx_.insns, 0, sizeof(block_ctx_.insns));

    uint32_t i = 0;
    bool delay_pending = false;  /* prior insn was a branch; this is its delay slot */
    for (; i < kMaxMipsInsnPerBlock && guest_pc < page_end; ++i, guest_pc += 4) {
        MipsDecodedInsn& insn = block_ctx_.insns[i];

        uint32_t pa = 0;
        if (mmu_->Translate(&cpu_state_, guest_pc, MipsAccess::kFetch, &pa) !=
            MipsTlbResult::kMatch) {
            break;  /* TLB/address fault */
        }
        uint8_t* host = memory_->TryTranslate(pa);
        if (!host) {
            break;  /* unmapped or I/O space: not executable */
        }

        uint32_t word;
        std::memcpy(&word, host, sizeof(word));
        if (decoder_.Decode(word, guest_pc, &insn)) {
            insn.place_fn = SelectPlaceFn(&insn);
        } else {
            /* Encoding not valid for this CPU's capabilities (COP1 when !HasFpu,
               LL/SC when !HasLlsc, or a reserved opcode) -> Reserved Instruction.
               CP0 exception delivery is not yet built, so it surfaces loudly. */
            insn.place_fn = &PlaceMipsUndefined;
        }

        if (delay_pending) {            /* this insn was the branch's delay slot */
            ++i;
            break;                      /* block ends after the delay slot */
        }
        if (insn.ends_block) {          /* ERET / HIBERNATE: no delay slot */
            ++i;
            break;
        }
        if (insn.is_branch) {
            delay_pending = true;       /* next insn is the delay slot, then end */
        }
    }

    /* A trailing branch (page end / cap before its delay slot) is KEPT, not
       dropped: its place fn set branch_state, the block exits with pc=branch+4,
       and the carried state resolves in the next block (QEMU DISAS_TOO_MANY +
       save_cpu_state). Dropping it was the cross-page "decoded 0 insns" defect. */
    block_ctx_.num_insns = i;
}

uint32_t MipsJit::BlockIndexKey(uint32_t phys_start) {
    uint8_t* host = memory_->TryTranslateWrite(phys_start);
    if (!host) return kBlockUnindexed;

    const uint32_t off = DramIndexOffset(host);
    if (off != UINT32_MAX) return off;
    if (InInjectionBand(host)) return kBlockUnindexed;
    LOG(Caution, "MipsJit::BlockIndexKey: block at pa=0x%08X pc=0x%08X is in a "
            "writable region that is neither DRAM nor the injection band; a store "
            "into it would not invalidate the block\n", phys_start, cpu_state_.pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void MipsJit::InvalidateOnRamWrite(uint8_t* host, uint32_t size) {
    const uint32_t off = DramIndexOffset(host);
    if (off == UINT32_MAX) {
        if (InInjectionBand(host)) return;
        if (InDmaRegion(host)) return;
        LOG(Caution, "MipsJit::InvalidateOnRamWrite: store of %u byte(s) at host %p "
                "pc=0x%08X lands in a writable region that is neither DRAM nor the "
                "injection band; self-modifying code there is unmodeled\n",
                size, host, cpu_state_.pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (blocks_.PageHasBlocks(off)) {
        blocks_.RemoveRange(off, off + size - 1u);
    }
    if (blocks16_.PageHasBlocks(off)) {
        blocks16_.RemoveRange(off, off + size - 1u);
    }
}
