#include "mips_mmu.h"

#include "../isa_block_space.h"

/* Faithful transcription of QEMU target/mips tlb_helper.c r4k_* for the VR5500
   (R4000-class, 32-bit kernel, MI/MMID and XI/RI/EHINV absent). TARGET_PAGE_BITS
   = 12, (TARGET_PAGE_MASK << 1) = 0xFFFFE000, so mask = PageMask | 0x1FFF. */

namespace {

constexpr uint32_t kVpn2Mask = 0xFFFFE000;  /* TARGET_PAGE_MASK << 1 */
constexpr uint32_t kAsidMask = 0x000000FF;  /* VR5500 EntryHi ASID */

/* get_tlb_pfn_from_entrylo (tlb_helper.c:42), 32-bit: extract64(entrylo,6,24).
   The PFNX half (bits >=32) does not exist in a 32-bit EntryLo. */
inline uint32_t TlbPfnFromEntryLo(uint32_t entrylo) {
    return (entrylo >> 6) & 0x00FFFFFFu;
}

/* get_entrylo_pfn_from_tlb (tlb_helper.c:225), 32-bit: extract64(tlb_pfn,0,24)<<6
   (the <<32 PFNX half is not representable in a 32-bit EntryLo). */
inline uint32_t EntryLoPfnFromTlb(uint64_t pfn) {
    const uint64_t tlb_pfn = pfn >> 12;
    return static_cast<uint32_t>((tlb_pfn & 0x00FFFFFFu) << 6);
}

}  // namespace

MipsTlbResult MipsMmu::MapAddress(MipsCpuState* st, uint32_t va, MipsAccess acc,
                                  uint32_t* pa) {
    const uint16_t asid = st->cp0_entryhi & kAsidMask;
    for (uint32_t i = 0; i < st->tlb_in_use; i++) {
        const MipsTlbEntry& e = st->tlb[i];
        const uint32_t mask = e.page_mask | 0x1FFFu;
        const uint32_t tag  = va & ~mask;
        const uint32_t vpn  = e.vpn & ~mask;
        if (!((e.g || e.asid == asid) && vpn == tag)) {
            continue;
        }
        const uint32_t n = (va & mask & ~(mask >> 1)) ? 1u : 0u;
        if (!(n ? e.v1 : e.v0)) {
            return MipsTlbResult::kInvalid;
        }
        if (acc != MipsAccess::kWrite || (n ? e.d1 : e.d0)) {
            *pa = static_cast<uint32_t>(e.pfn[n] | (va & (mask >> 1)));
            return MipsTlbResult::kMatch;
        }
        return MipsTlbResult::kModified;
    }
    return MipsTlbResult::kNoMatch;
}

MipsTlbResult MipsMmu::Translate(MipsCpuState* st, uint32_t va, MipsAccess acc,
                                 uint32_t* pa) {
    if (va < MipsSeg::kKusegEnd) {
        return MapAddress(st, va, acc, pa);         /* kuseg */
    }
    if (va < MipsSeg::kKseg1Base) {                 /* kseg0: unmapped cached */
        *pa = va & MipsSeg::kUnmappedMask;
        return MipsTlbResult::kMatch;
    }
    if (va < MipsSeg::kKseg2Base) {                 /* kseg1: unmapped uncached */
        *pa = va & MipsSeg::kUnmappedMask;
        return MipsTlbResult::kMatch;
    }
    return MapAddress(st, va, acc, pa);             /* kseg2/kseg3 */
}

bool MipsMmu::ExecPageGlobal(MipsCpuState* st, uint32_t va) {
    /* kseg0/kseg1 (unmapped kernel windows) are global. */
    if (va >= MipsSeg::kKusegEnd && va < MipsSeg::kKseg2Base) {
        return true;
    }
    /* Mapped segment: the matching live TLB entry's G bit (r4k_map_address match
       predicate, tlb_helper.c:393). No match -> not global. */
    const uint16_t asid = st->cp0_entryhi & kAsidMask;
    for (uint32_t i = 0; i < st->tlb_in_use; i++) {
        const MipsTlbEntry& e = st->tlb[i];
        const uint32_t mask = e.page_mask | 0x1FFFu;
        if ((e.g || e.asid == asid) && (e.vpn & ~mask) == (va & ~mask)) {
            return e.g != 0;
        }
    }
    return false;
}

void MipsMmu::FillTlb(MipsCpuState* st, uint32_t idx) {
    /* r4k_fill_tlb (tlb_helper.c:52). mask = CP0_PageMask >> (TARGET_PAGE_BITS+1). */
    const uint32_t mask = st->cp0_pagemask >> 13;
    MipsTlbEntry& e = st->tlb[idx];
    e.vpn       = st->cp0_entryhi & kVpn2Mask;
    e.asid      = st->cp0_entryhi & kAsidMask;
    e.page_mask = st->cp0_pagemask;
    e.g  = static_cast<uint8_t>(st->cp0_entrylo0 & st->cp0_entrylo1 & 1u);
    e.v0 = static_cast<uint8_t>((st->cp0_entrylo0 & 2u) != 0);
    e.d0 = static_cast<uint8_t>((st->cp0_entrylo0 & 4u) != 0);
    e.c0 = static_cast<uint8_t>((st->cp0_entrylo0 >> 3) & 7u);
    e.pfn[0] = (static_cast<uint64_t>(TlbPfnFromEntryLo(st->cp0_entrylo0) & ~mask)) << 12;
    e.v1 = static_cast<uint8_t>((st->cp0_entrylo1 & 2u) != 0);
    e.d1 = static_cast<uint8_t>((st->cp0_entrylo1 & 4u) != 0);
    e.c1 = static_cast<uint8_t>((st->cp0_entrylo1 >> 3) & 7u);
    e.pfn[1] = (static_cast<uint64_t>(TlbPfnFromEntryLo(st->cp0_entrylo1) & ~mask)) << 12;
}

void MipsMmu::InvalidateTlb(MipsCpuState* st, uint32_t idx, bool use_extra) {
    /* r4k_invalidate_tlb (tlb_helper.c:1378). */
    MipsTlbEntry& e = st->tlb[idx];
    const uint16_t asid = st->cp0_entryhi & kAsidMask;
    /* A non-global entry whose ASID differs from the current one is skipped
       (tlb_helper.c:1393). */
    if (e.g == 0 && e.asid != asid) {
        return;
    }
    if (use_extra && st->tlb_in_use < kMipsTlbMax) {
        /* tlbwr: shadow the discarded entry into a fake slot (tlb_helper.c:1402). */
        st->tlb[st->tlb_in_use] = e;
        st->tlb_in_use++;
        return;
    }
    const uint32_t mask = e.page_mask | 0x1FFFu;
    if (e.v0) {                                      /* even sub-page (tlb_helper.c:1415) */
        uint32_t addr = e.vpn & ~mask;
        const uint32_t end = addr | (mask >> 1);
        while (addr < end) {
            blocks_->JumpCacheClearPage(addr);
            addr += 0x1000u;
        }
    }
    if (e.v1) {                                      /* odd sub-page (tlb_helper.c:1428) */
        uint32_t addr = (e.vpn & ~mask) | ((mask >> 1) + 1u);
        const uint32_t end = addr | mask;
        while (addr - 1u < end) {
            blocks_->JumpCacheClearPage(addr);
            addr += 0x1000u;
        }
    }
}

void MipsMmu::FlushExtra(MipsCpuState* st, uint32_t first) {
    /* r4k_mips_tlb_flush_extra (tlb_helper.c:34): discard shadow entries. */
    while (st->tlb_in_use > first) {
        InvalidateTlb(st, --st->tlb_in_use, false);
    }
}

void MipsMmu::WriteIndexed(MipsCpuState* st) {
    /* r4k_helper_tlbwi (tlb_helper.c:116). */
    const uint16_t asid = st->cp0_entryhi & kAsidMask;
    const uint32_t idx  = (st->cp0_index & ~0x80000000u) % st->nb_tlb;
    const MipsTlbEntry& e = st->tlb[idx];
    const uint32_t vpn = st->cp0_entryhi & kVpn2Mask;
    const uint8_t  g   = static_cast<uint8_t>(st->cp0_entrylo0 & st->cp0_entrylo1 & 1u);
    const bool v0 = (st->cp0_entrylo0 & 2u) != 0;
    const bool d0 = (st->cp0_entrylo0 & 4u) != 0;
    const bool v1 = (st->cp0_entrylo1 & 2u) != 0;
    const bool d1 = (st->cp0_entrylo1 & 4u) != 0;
    /* Discard cached entries unless tlbwi only upgrades permissions on the
       current entry (tlb_helper.c:147). XI/RI/EHINV terms omitted (VR5500). */
    if (e.vpn != vpn || e.asid != asid || e.g != g ||
        (e.v0 && !v0) || (e.d0 && !d0) ||
        (e.v1 && !v1) || (e.d1 && !d1)) {
        FlushExtra(st, st->nb_tlb);
    }
    InvalidateTlb(st, idx, false);
    FillTlb(st, idx);
}

uint32_t MipsMmu::RandomIndex(MipsCpuState* st) {
    /* cpu_mips_get_random (cp0_helper.c:204): a Linear Congruential Generator
       (ISO/IEC 9899) over [Wired, nb_tlb-1], never returning the previous index. */
    const uint32_t nb_rand = st->nb_tlb - st->cp0_wired;
    if (nb_rand == 1) {
        return st->nb_tlb - 1u;
    }
    uint32_t idx;
    do {
        lcg_seed_ = 1103515245u * lcg_seed_ + 12345u;
        idx = (lcg_seed_ >> 16) % nb_rand + st->cp0_wired;
    } while (idx == prev_random_idx_);
    prev_random_idx_ = idx;
    return idx;
}

void MipsMmu::WriteRandom(MipsCpuState* st) {
    /* r4k_helper_tlbwr (tlb_helper.c:164): write the entry at a random index,
       shadowing the discarded one (use_extra) so a probe can still find it. */
    const uint32_t r = RandomIndex(st);
    InvalidateTlb(st, r, true);
    FillTlb(st, r);
}

void MipsMmu::Probe(MipsCpuState* st) {
    /* r4k_helper_tlbp (tlb_helper.c:172). */
    const uint16_t asid = st->cp0_entryhi & kAsidMask;
    for (uint32_t i = 0; i < st->nb_tlb; i++) {
        const MipsTlbEntry& e = st->tlb[i];
        const uint32_t mask = e.page_mask | 0x1FFFu;
        const uint32_t tag  = st->cp0_entryhi & ~mask;
        const uint32_t vpn  = e.vpn & ~mask;
        if ((e.g || e.asid == asid) && vpn == tag) {
            st->cp0_index = i;
            return;
        }
    }
    /* No live match: discard the first matching shadow entry (tlb_helper.c:202). */
    for (uint32_t i = st->nb_tlb; i < st->tlb_in_use; i++) {
        const MipsTlbEntry& e = st->tlb[i];
        const uint32_t mask = e.page_mask | 0x1FFFu;
        const uint32_t tag  = st->cp0_entryhi & ~mask;
        const uint32_t vpn  = e.vpn & ~mask;
        if ((e.g || e.asid == asid) && vpn == tag) {
            FlushExtra(st, i);
            break;
        }
    }
    st->cp0_index |= 0x80000000u;  /* probe failed: P bit */
}

void MipsMmu::Read(MipsCpuState* st) {
    /* r4k_helper_tlbr (tlb_helper.c:235). */
    const uint16_t asid = st->cp0_entryhi & kAsidMask;
    const uint32_t idx  = (st->cp0_index & ~0x80000000u) % st->nb_tlb;
    const MipsTlbEntry& e = st->tlb[idx];
    if (asid != e.asid) {                            /* ASID change -> whole flush (tlb_helper.c:249) */
        FlushAll(st);
    }
    FlushExtra(st, st->nb_tlb);
    st->cp0_entryhi  = e.vpn | e.asid;
    st->cp0_pagemask = e.page_mask;
    st->cp0_entrylo0 = e.g | (static_cast<uint32_t>(e.v0) << 1) |
                       (static_cast<uint32_t>(e.d0) << 2) |
                       (static_cast<uint32_t>(e.c0) << 3) | EntryLoPfnFromTlb(e.pfn[0]);
    st->cp0_entrylo1 = e.g | (static_cast<uint32_t>(e.v1) << 1) |
                       (static_cast<uint32_t>(e.d1) << 2) |
                       (static_cast<uint32_t>(e.c1) << 3) | EntryLoPfnFromTlb(e.pfn[1]);
}

void MipsMmu::FlushAll(MipsCpuState* st) {
    /* cpu_mips_tlb_flush (tlb_helper.c:492): flush the host translation cache +
       discard all shadow entries. */
    blocks_->JumpCacheFlush();
    st->tlb_in_use = st->nb_tlb;
}
