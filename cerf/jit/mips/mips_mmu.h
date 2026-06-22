#pragma once

#include <cstdint>

#include "mips_cpu_state.h"

namespace MipsSeg {
    constexpr uint32_t kKusegEnd  = 0x80000000;  /* kuseg  0x00000000..0x7FFFFFFF, TLB-mapped */
    constexpr uint32_t kKseg0Base = 0x80000000;  /* kseg0  ..0x9FFFFFFF, unmapped cached */
    constexpr uint32_t kKseg1Base = 0xA0000000;  /* kseg1  ..0xBFFFFFFF, unmapped uncached */
    constexpr uint32_t kKseg2Base = 0xC0000000;  /* kseg2/3 ..0xFFFFFFFF, TLB-mapped */
    constexpr uint32_t kUnmappedMask = 0x1FFFFFFF;  /* kseg0/1 VA -> PA */
}

/* EntryLo0/1 bit layout (R4000), per QEMU tlb_helper.c r4k_fill_tlb. */
namespace MipsEntryLo {
    constexpr uint32_t kGlobal   = 1u << 0;   /* G - matches any ASID (must be set in both halves) */
    constexpr uint32_t kValid    = 1u << 1;   /* V */
    constexpr uint32_t kDirty    = 1u << 2;   /* D - writable */
    constexpr uint32_t kCacheShift = 3;       /* C, bits 5..3 */
    constexpr uint32_t kPfnShift   = 6;       /* PFN, bits 25..6 -> PA[31:12] */
}

/* Outcome of a translation attempt; a non-MATCH result is delivered to the
   guest as the corresponding CP0 exception (refill / invalid / modified). */
enum class MipsTlbResult {
    kMatch,     /* pa valid */
    kNoMatch,   /* TLB miss     -> TLB-refill exception */
    kInvalid,   /* entry V=0    -> TLB-invalid exception */
    kModified,  /* store, D=0   -> TLB-modified exception */
};

enum class MipsAccess { kFetch, kRead, kWrite };

class MipsMmu {
public:
    /* Translate a guest VA. kseg0/kseg1 resolve directly; kuseg/kseg2 walk the
       software TLB (ASID from CP0_EntryHi). On kMatch, *pa holds the physical
       address; otherwise the caller raises the indicated CP0 exception. */
    MipsTlbResult Translate(MipsCpuState* st, uint32_t va, MipsAccess acc, uint32_t* pa);

    /* CP0 TLB instructions: write the indexed/random entry from EntryHi/
       EntryLo0/EntryLo1/PageMask (tlbwi/tlbwr), read an indexed entry back into
       those regs (tlbr), and probe for a VPN/ASID match setting CP0_Index
       (tlbp, Index bit31 set on no-match). */
    void WriteIndexed(MipsCpuState* st);
    void WriteRandom(MipsCpuState* st);
    void Read(MipsCpuState* st);
    void Probe(MipsCpuState* st);
};
