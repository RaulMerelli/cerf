#include "mips_mmu.h"

/* R4000 software-TLB translation + the four CP0 TLB instructions, modelled on
   QEMU target/mips tlb_helper.c (r4k_map_address / r4k_fill_tlb / tlbwi / tlbwr
   / tlbp / tlbr). VR5500 is R4000-class: no XI/RI execute/read-inhibit and no
   EHINV, so those bits are intentionally not modelled. */

namespace {

constexpr uint32_t kVpn2Mask = 0xFFFFE000;  /* TARGET_PAGE_MASK(4K) << 1 */
constexpr uint32_t kAsidMask = 0x000000FF;  /* VR5500 ASID = EntryHi[7:0] */

/* Walk the 48-entry TLB for a mapped-segment VA. */
MipsTlbResult TlbLookup(MipsCpuState* st, uint32_t va, MipsAccess acc, uint32_t* pa) {
    uint32_t asid = st->cp0_entryhi & kAsidMask;
    for (uint32_t i = 0; i < kMipsTlbEntries; i++) {
        const MipsTlbEntry& e = st->tlb[i];
        uint32_t mask = e.page_mask | 0x1FFF;
        uint32_t vpn  = (e.entry_hi & kVpn2Mask) & ~mask;
        uint32_t tag  = va & ~mask;
        bool global   = (e.entry_lo0 & e.entry_lo1 & MipsEntryLo::kGlobal) != 0;
        if (!((global || (e.entry_hi & kAsidMask) == asid) && vpn == tag)) {
            continue;
        }
        uint32_t n  = (va & mask & ~(mask >> 1)) ? 1u : 0u;
        uint32_t lo = n ? e.entry_lo1 : e.entry_lo0;
        if (!(lo & MipsEntryLo::kValid)) {
            return MipsTlbResult::kInvalid;
        }
        if (acc == MipsAccess::kWrite && !(lo & MipsEntryLo::kDirty)) {
            return MipsTlbResult::kModified;
        }
        uint32_t page_base = ((lo >> MipsEntryLo::kPfnShift) << 12) & ~(mask >> 1);
        *pa = page_base | (va & (mask >> 1));
        return MipsTlbResult::kMatch;
    }
    return MipsTlbResult::kNoMatch;
}

}  // namespace

MipsTlbResult MipsMmu::Translate(MipsCpuState* st, uint32_t va, MipsAccess acc, uint32_t* pa) {
    if (va < MipsSeg::kKusegEnd) {
        return TlbLookup(st, va, acc, pa);          /* kuseg */
    }
    if (va < MipsSeg::kKseg1Base) {                 /* kseg0: unmapped cached */
        *pa = va & MipsSeg::kUnmappedMask;
        return MipsTlbResult::kMatch;
    }
    if (va < MipsSeg::kKseg2Base) {                 /* kseg1: unmapped uncached */
        *pa = va & MipsSeg::kUnmappedMask;
        return MipsTlbResult::kMatch;
    }
    return TlbLookup(st, va, acc, pa);              /* kseg2/kseg3 */
}

MipsTlbEntry MipsMmu::WriteIndexed(MipsCpuState* st) {
    uint32_t idx = (st->cp0_index & 0x7FFFFFFF) % kMipsTlbEntries;
    MipsTlbEntry& e = st->tlb[idx];
    const MipsTlbEntry old = e;
    e.entry_hi   = st->cp0_entryhi;
    e.page_mask  = st->cp0_pagemask;
    e.entry_lo0  = st->cp0_entrylo0;
    e.entry_lo1  = st->cp0_entrylo1;
    return old;
}

void MipsMmu::WriteRandom(MipsCpuState* st) {
    uint32_t idx = st->cp0_random % kMipsTlbEntries;
    MipsTlbEntry& e = st->tlb[idx];
    e.entry_hi   = st->cp0_entryhi;
    e.page_mask  = st->cp0_pagemask;
    e.entry_lo0  = st->cp0_entrylo0;
    e.entry_lo1  = st->cp0_entrylo1;
}

void MipsMmu::Read(MipsCpuState* st) {
    uint32_t idx = (st->cp0_index & 0x7FFFFFFF) % kMipsTlbEntries;
    const MipsTlbEntry& e = st->tlb[idx];
    st->cp0_entryhi  = e.entry_hi;
    st->cp0_pagemask = e.page_mask;
    st->cp0_entrylo0 = e.entry_lo0;
    st->cp0_entrylo1 = e.entry_lo1;
}

void MipsMmu::Probe(MipsCpuState* st) {
    uint32_t asid = st->cp0_entryhi & kAsidMask;
    for (uint32_t i = 0; i < kMipsTlbEntries; i++) {
        const MipsTlbEntry& e = st->tlb[i];
        uint32_t mask = e.page_mask | 0x1FFF;
        uint32_t vpn  = (e.entry_hi & kVpn2Mask) & ~mask;
        uint32_t tag  = (st->cp0_entryhi & kVpn2Mask) & ~mask;
        bool global   = (e.entry_lo0 & e.entry_lo1 & MipsEntryLo::kGlobal) != 0;
        if ((global || (e.entry_hi & kAsidMask) == asid) && vpn == tag) {
            st->cp0_index = i;
            return;
        }
    }
    st->cp0_index |= 0x80000000;  /* probe failed: P bit (Index[31]) */
}
