#pragma once

#include <cstdint>

#include "mips_cpu_state.h"

struct IsaBlockSpace;

namespace MipsSeg {
    constexpr uint32_t kKusegEnd     = 0x80000000;  /* kuseg  0x00000000..0x7FFFFFFF, TLB-mapped */
    constexpr uint32_t kKseg1Base    = 0xA0000000;  /* kseg0  ..0x9FFFFFFF, unmapped cached */
    constexpr uint32_t kKseg2Base    = 0xC0000000;  /* kseg1  ..0xBFFFFFFF, unmapped uncached */
    constexpr uint32_t kUnmappedMask = 0x1FFFFFFF;  /* kseg0/1 VA -> PA */
}

/* EntryLo0/1 bit layout (R4000), per QEMU tlb_helper.c r4k_fill_tlb. */
namespace MipsEntryLo {
    constexpr uint32_t kGlobal = 1u << 0;   /* G - matches any ASID (set in both halves) */
    constexpr uint32_t kValid  = 1u << 1;   /* V */
    constexpr uint32_t kDirty  = 1u << 2;   /* D - writable */
}

/* Outcome of a translation attempt; a non-MATCH result is delivered to the
   guest as the corresponding CP0 exception (refill / invalid / modified). */
enum class MipsTlbResult {
    kMatch,     /* pa valid (TLBRET_MATCH) */
    kNoMatch,   /* TLB miss   -> TLB-refill   (TLBRET_NOMATCH) */
    kInvalid,   /* entry V=0  -> TLB-invalid  (TLBRET_INVALID) */
    kModified,  /* store, D=0 -> TLB-modified (TLBRET_DIRTY) */
};

enum class MipsAccess { kFetch, kRead, kWrite };

/* R4000 software TLB + CP0 tlbwi/tlbwr/tlbp/tlbr, modelled on QEMU target/mips
   tlb_helper.c (r4k_*). VR5500 omits MMID/XI/RI/EHINV. The JIT block cache is
   CERF's host-softmmu-TLB analog: QEMU tlb_flush_page/tlb_flush map to
   IsaBlockSpace::JumpCacheClearPage/JumpCacheFlush. */
class MipsMmu {
public:
    /* The JIT block cache MipsMmu invalidates as QEMU's tlb_flush_page would;
       bound in MipsJit::OnReady. */
    void Bind(IsaBlockSpace* blocks) { blocks_ = blocks; }

    /* Translate a guest VA. kseg0/kseg1 resolve directly; kuseg/kseg2 walk the
       software TLB (ASID from CP0_EntryHi). On kMatch, *pa holds the physical
       address; otherwise the caller raises the indicated CP0 exception. */
    MipsTlbResult Translate(MipsCpuState* st, uint32_t va, MipsAccess acc, uint32_t* pa);

    /* True iff va's page is global (kseg0/kseg1, or a TLB entry with G=1). The
       JIT routes a global page's block to the shared `global` index, else to
       per_asid[EntryHi.ASID]. */
    bool ExecPageGlobal(MipsCpuState* st, uint32_t va);

    /* CP0 TLB instructions, faithful to QEMU r4k_helper_tlb{wi,wr,p,r}. */
    void WriteIndexed(MipsCpuState* st);   /* tlbwi */
    void WriteRandom (MipsCpuState* st);   /* tlbwr */
    void Probe       (MipsCpuState* st);   /* tlbp  */
    void Read        (MipsCpuState* st);   /* tlbr  */

    /* cpu_mips_tlb_flush: drop the whole jump cache + discard all shadow
       entries (tlb_helper.c:492). Called on a CP0 ASID change. */
    void FlushAll(MipsCpuState* st);

private:
    /* r4k_map_address (tlb_helper.c:393) - the mapped-segment TLB walk over
       tlb_in_use (live + shadow) entries. */
    MipsTlbResult MapAddress(MipsCpuState* st, uint32_t va, MipsAccess acc, uint32_t* pa);

    void FillTlb     (MipsCpuState* st, uint32_t idx);                /* r4k_fill_tlb */
    void InvalidateTlb(MipsCpuState* st, uint32_t idx, bool use_extra); /* r4k_invalidate_tlb */
    void FlushExtra  (MipsCpuState* st, uint32_t first);             /* r4k_mips_tlb_flush_extra */

    /* cpu_mips_get_random (cp0_helper.c:204): the tlbwr index. QEMU keeps the LCG
       seed + last index in process statics; here they are per-instance state. */
    uint32_t RandomIndex(MipsCpuState* st);

    IsaBlockSpace* blocks_ = nullptr;
    uint32_t lcg_seed_        = 1;
    uint32_t prev_random_idx_ = 0;
};
