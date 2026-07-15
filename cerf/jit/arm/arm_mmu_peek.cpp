#include "arm_mmu.h"

#include "../../cpu/arm_processor_config.h"
#include "../../cpu/emulated_memory.h"
#include "arm_pte.h"

/* Functional resolver, not diagnostic-only: the guest-additions accelerator
   (cerf_virt gpe_cmd / blitter) reads & writes guest memory through it. MUST
   stay side-effect-free (no TLB fill, no abort raise, null on untranslatable)
   or a host-side accelerator read faults/perturbs the running guest. */
std::optional<uint32_t> ArmMmu::WalkVaToPa(uint32_t va) {
    uint32_t p = va;

    /* FCSE fold: low-32-MB VAs are private to the current process,
       same fold MapGuestVirtualToHost applies. */
    if ((p & 0xFE000000u) == 0u) p |= state_.process_id;

    const uint32_t ttbcr_n    = state_.ttbcr & 7u;
    const uint32_t ttbr0_mask = ~((1u << (14u - ttbcr_n)) - 1u);
    const bool use_ttbr1 = ttbcr_n != 0u && (p >> (32u - ttbcr_n)) != 0u;
    const uint32_t l1_base = use_ttbr1
        ? (state_.ttbr1 & 0xFFFFC000u)
        : (state_.translation_table_base.word & ttbr0_mask);

    const uint32_t l1_pa = l1_base | ((p >> 20) << 2);
    uint8_t* l1_host = memory_->TryTranslateWrite(l1_pa);
    if (!l1_host) return std::nullopt;
    ArmL1Pte l1_pte;
    l1_pte.word = *reinterpret_cast<uint32_t*>(l1_host);

    switch (l1_pte.fault.type) {
    case ArmL1PteType::kSection:
        return (l1_pte.section.section_base << 20) | (p & 0x000FFFFFu);

    case ArmL1PteType::kCoarse: {
        const uint32_t l2_pa = (l1_pte.coarse.page_table_base << 10)
                             | (((p >> 12) & 0xFFu) << 2);
        uint8_t* l2_host = memory_->TryTranslateWrite(l2_pa);
        if (!l2_host) return std::nullopt;
        ArmL2Pte l2_pte;
        l2_pte.word = *reinterpret_cast<uint32_t*>(l2_host);

        const bool v6_ext_small = processor_config_->HasCp15V6() &&
                                  !state_.control_register.bits.xp;
        if (l2_pte.fault.type == ArmL2PteType::kSmallPage) {
            return (l2_pte.small_page.small_page_base << 12) | (p & 0x0FFFu);
        }
        if (l2_pte.fault.type == ArmL2PteType::kExtendedSmallPage && v6_ext_small) {
            return ArmExtSmallPagePa(l2_pte.word, p);
        }
        return std::nullopt;
    }

    default:
        return std::nullopt;
    }
}

uint8_t* ArmMmu::PeekVaToHost(uint32_t va) {
    if (!state_.control_register.bits.m) {
        uint8_t* ram = memory_->TryTranslateWrite(va);
        return ram ? ram : memory_->TryTranslate(va);
    }

    /* TLB before walk, mirroring the real walker. WinCE lazily zeroes the
       in-memory L2 of a PSL server's home-slot stack while the server runs
       under a different active FCSE process; the page stays TLB-resident, so a
       walk-only peek returns nullptr for a page the guest still reads fine. */
    if (std::optional<uint8_t*> tlb = PeekDataTlb(va)) return *tlb;

    std::optional<uint32_t> pa = WalkVaToPa(va);
    if (!pa) return nullptr;
    uint8_t* ram = memory_->TryTranslateWrite(*pa);
    return ram ? ram : memory_->TryTranslate(*pa);
}

bool ArmMmu::PeekVaToPa(uint32_t va, uint32_t* pa) {
    if (!state_.control_register.bits.m) { *pa = va; return true; }

    uint32_t p = 0;
    if (const ArmTlbEntry* e = MatchDataTlb(va, &p)) {
        *pa = e->pa_page | (p & 0x0FFFu);
        return true;
    }

    std::optional<uint32_t> walked = WalkVaToPa(va);
    if (!walked) return false;
    *pa = *walked;
    return true;
}
