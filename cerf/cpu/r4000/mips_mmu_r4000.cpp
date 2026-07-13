#include "../../jit/mips/mips_mmu.h"

#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../jit/isa_block_space.h"

/* QEMU target/mips tlb_helper.c r4k_*, parameterized on the SoC min-page shift
   S = MipsCpuState::min_page_shift (QEMU TARGET_PAGE_BITS): a PFN gives PA[31:S]
   and the VPN2 pair spans 2^(S+1). VR5500 S=12 (4 KB), VR4102 S=10 (1 KB, UM
   5.2.2). MI/MMID and XI/RI/EHINV absent (32-bit kernel). */

namespace {

/* 8-bit EntryHi ASID: VR5500, VR4102, VR4121 (VR4121 UM Fig 6-17). */
constexpr uint32_t kAsidMask = 0x000000FF;

/* get_tlb_pfn_from_entrylo (tlb_helper.c:42), 32-bit: extract64(entrylo,6,24). */
inline uint32_t TlbPfnFromEntryLo(uint32_t entrylo) {
    return (entrylo >> 6) & 0x00FFFFFFu;
}

/* get_entrylo_pfn_from_tlb (tlb_helper.c:225), 32-bit: extract64(tlb_pfn,0,24)<<6. */
inline uint32_t EntryLoPfnFromTlb(uint64_t pfn, uint32_t s) {
    const uint64_t tlb_pfn = pfn >> s;
    return static_cast<uint32_t>((tlb_pfn & 0x00FFFFFFu) << 6);
}

class MipsMmuR4000 : public MipsMmu {
public:
    using MipsMmu::MipsMmu;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && (bd->GetSoc() == SocFamily::VR4102 ||
                      bd->GetSoc() == SocFamily::VR4121 ||
                      bd->GetSoc() == SocFamily::VR5500);
    }

    void WriteIndexed(MipsCpuState* st) override {
        /* r4k_helper_tlbwi (tlb_helper.c:116). */
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        const uint32_t idx  = (st->cp0_index & ~0x80000000u) % st->nb_tlb;
        const MipsTlbEntry& e = st->tlb[idx];
        const uint32_t vpn = st->cp0_entryhi & MipsVpn2Mask(st->min_page_shift);
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

    void WriteRandom(MipsCpuState* st) override {
        /* r4k_helper_tlbwr (tlb_helper.c:164): write the entry at a random index,
           shadowing the discarded one (use_extra) so a probe can still find it. */
        const uint32_t r = RandomIndex(st);
        InvalidateTlb(st, r, true);
        FillTlb(st, r);
    }

    void Probe(MipsCpuState* st) override {
        /* r4k_helper_tlbp (tlb_helper.c:172). */
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        for (uint32_t i = 0; i < st->nb_tlb; i++) {
            const MipsTlbEntry& e = st->tlb[i];
            const uint32_t mask = e.page_mask | MipsPairMinMask(st->min_page_shift);
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
            const uint32_t mask = e.page_mask | MipsPairMinMask(st->min_page_shift);
            const uint32_t tag  = st->cp0_entryhi & ~mask;
            const uint32_t vpn  = e.vpn & ~mask;
            if ((e.g || e.asid == asid) && vpn == tag) {
                FlushExtra(st, i);
                break;
            }
        }
        st->cp0_index |= 0x80000000u;  /* probe failed: P bit */
    }

    void Read(MipsCpuState* st) override {
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
                           (static_cast<uint32_t>(e.c0) << 3) | EntryLoPfnFromTlb(e.pfn[0], st->min_page_shift);
        st->cp0_entrylo1 = e.g | (static_cast<uint32_t>(e.v1) << 1) |
                           (static_cast<uint32_t>(e.d1) << 2) |
                           (static_cast<uint32_t>(e.c1) << 3) | EntryLoPfnFromTlb(e.pfn[1], st->min_page_shift);
    }

    uint32_t RandomIndex(MipsCpuState* st) override {
        return NextRandom(st->cp0_wired, st->nb_tlb - st->cp0_wired);
    }

protected:
    MipsTlbResult MapAddress(MipsCpuState* st, uint32_t va, MipsAccess acc,
                             uint32_t* pa) override {
        /* r4k_map_address (tlb_helper.c:393). */
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        for (uint32_t i = 0; i < st->tlb_in_use; i++) {
            const MipsTlbEntry& e = st->tlb[i];
            const uint32_t mask = e.page_mask | MipsPairMinMask(st->min_page_shift);
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
                /* SoC physical-space mirror (VR4102 UM Table 5-6): a PFN above
                   0x20000000 aliases into 0x0-0x1FFFFFFF. */
                *pa = static_cast<uint32_t>(e.pfn[n] | (va & (mask >> 1))) & st->phys_addr_mask;
                return MipsTlbResult::kMatch;
            }
            return MipsTlbResult::kModified;
        }
        return MipsTlbResult::kNoMatch;
    }

    bool MappedPageGlobal(MipsCpuState* st, uint32_t va) override {
        /* r4k_map_address match predicate (tlb_helper.c:393). */
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        for (uint32_t i = 0; i < st->tlb_in_use; i++) {
            const MipsTlbEntry& e = st->tlb[i];
            const uint32_t mask = e.page_mask | MipsPairMinMask(st->min_page_shift);
            if ((e.g || e.asid == asid) && (e.vpn & ~mask) == (va & ~mask)) {
                return e.g != 0;
            }
        }
        return false;
    }

private:
    void FillTlb(MipsCpuState* st, uint32_t idx) {
        /* r4k_fill_tlb (tlb_helper.c:52). mask = CP0_PageMask >> (min_page_shift+1). */
        const uint32_t mask = st->cp0_pagemask >> (st->min_page_shift + 1u);
        MipsTlbEntry& e = st->tlb[idx];
        e.vpn       = st->cp0_entryhi & MipsVpn2Mask(st->min_page_shift);
        e.asid      = st->cp0_entryhi & kAsidMask;
        e.page_mask = st->cp0_pagemask;
        e.g  = static_cast<uint8_t>(st->cp0_entrylo0 & st->cp0_entrylo1 & 1u);
        e.v0 = static_cast<uint8_t>((st->cp0_entrylo0 & 2u) != 0);
        e.d0 = static_cast<uint8_t>((st->cp0_entrylo0 & 4u) != 0);
        e.c0 = static_cast<uint8_t>((st->cp0_entrylo0 >> 3) & 7u);
        e.pfn[0] = (static_cast<uint64_t>(TlbPfnFromEntryLo(st->cp0_entrylo0) & ~mask)) << st->min_page_shift;
        e.v1 = static_cast<uint8_t>((st->cp0_entrylo1 & 2u) != 0);
        e.d1 = static_cast<uint8_t>((st->cp0_entrylo1 & 4u) != 0);
        e.c1 = static_cast<uint8_t>((st->cp0_entrylo1 >> 3) & 7u);
        e.pfn[1] = (static_cast<uint64_t>(TlbPfnFromEntryLo(st->cp0_entrylo1) & ~mask)) << st->min_page_shift;
    }

    void InvalidateTlb(MipsCpuState* st, uint32_t idx, bool use_extra) {
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
        const uint32_t mask = e.page_mask | MipsPairMinMask(st->min_page_shift);
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

    void FlushExtra(MipsCpuState* st, uint32_t first) {
        /* r4k_mips_tlb_flush_extra (tlb_helper.c:34): discard shadow entries. */
        while (st->tlb_in_use > first) {
            InvalidateTlb(st, --st->tlb_in_use, false);
        }
    }
};

}  // namespace

REGISTER_SERVICE_AS(MipsMmuR4000, MipsMmu);
