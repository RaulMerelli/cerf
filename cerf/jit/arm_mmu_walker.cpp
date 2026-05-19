#include "arm_mmu.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../cpu/arm_processor_config.h"
#include "../cpu/emulated_memory.h"
#include "arm_cpu_exceptions.h"
#include "arm_pte.h"
#include "arm_tlb_ops.h"

namespace {

template <ArmMmuAccess kAccess>
inline bool ApPermits(uint32_t page_ap, bool is_user_mode) {
    if constexpr (kAccess == ArmMmuAccess::kRead ||
                  kAccess == ArmMmuAccess::kExecute) {
        return !(is_user_mode && page_ap == 1u);
    } else if constexpr (kAccess == ArmMmuAccess::kReadWrite ||
                         kAccess == ArmMmuAccess::kWrite) {
        if (is_user_mode) return page_ap == 3u;
        return page_ap != 0u;
    } else {
        return false;
    }
}

/* v6+ AP[2:0]: APX=ap[2], base=ap[1:0]. Covers the 5 AP values the
   WinCE7 NK kernel writes via PG_V6_PROT_* (vmarm.h:43-47) and
   ARMV6_MMU_PTL2_KR0 (ksarm.h:77). AP=000/100/111 unused-by-kernel —
   denying them keeps a stray walk fail-closed. */
template <ArmMmuAccess kAccess>
inline bool ApPermitsV6(uint32_t ap, bool is_user_mode) {
    if (ap == 0u || ap == 4u) return false;
    const bool apx = (ap & 4u) != 0u;
    const uint32_t base = ap & 3u;
    if constexpr (kAccess == ArmMmuAccess::kRead ||
                  kAccess == ArmMmuAccess::kExecute) {
        return is_user_mode ? base >= 2u : true;
    } else if constexpr (kAccess == ArmMmuAccess::kReadWrite ||
                         kAccess == ArmMmuAccess::kWrite) {
        if (apx) return false;
        return is_user_mode ? base == 3u : true;
    } else {
        return false;
    }
}

}  // namespace

template <ArmMmuAccess kAccess>
uint8_t* ArmMmu::MapGuestVirtualToHost(ArmCpuState* cpu_state, uint32_t p, int8_t* tlb_index_hint) {
    constexpr bool kIsWrite = (kAccess == ArmMmuAccess::kWrite ||
                               kAccess == ArmMmuAccess::kReadWrite);
    io_pending_address_        = 0;
    io_pending_address_adjust_ = 0;

    if (!state_.control_register.bits.m) {
        uint8_t* host = memory_->TryTranslate(p);
        if (host) return host;
        SetIoPending(p);
        return nullptr;
    }

    /* FCSE fold: low-32-MB VAs are private to the current process. */
    if ((p & 0xFE000000u) == 0u) {
        p |= state_.process_id;
    }

    ArmTlbUnit* tlb_unit = (kAccess == ArmMmuAccess::kExecute)
        ? &state_.instruction_tlb
        : &state_.data_tlb;

    const bool is_user_mode = (cpu_state->cpsr.bits.mode == ArmMode::kUser);
    uint32_t effective_address = 0;
    ArmTlbSlot* tlb_slot = nullptr;

    int8_t idx = *tlb_index_hint;
    for (size_t i = 0; i < kArmTlbSlotCount; ++i, idx = static_cast<int8_t>((idx + 1) % kArmTlbSlotCount)) {
        ArmTlbSlot& slot = tlb_unit->slots[idx];
        if (!slot.valid) continue;
        ArmL2Pte cached_pte;
        cached_pte.word = slot.pte;

        switch (cached_pte.fault.type) {
        case 0: {
            /* Section PTE (1 MB) — VA[31:20] compared. */
            if ((p >> 20) != slot.virtual_address) break;
            ArmL1Pte l1_pte;
            l1_pte.word = slot.pte;
            bool ap_ok;
            if (processor_config_->HasCp15V7()) {
                const uint32_t ap = ((l1_pte.word >> 10) & 3u) |
                                    (((l1_pte.word >> 15) & 1u) << 2);
                ap_ok = ApPermitsV6<kAccess>(ap, is_user_mode);
            } else {
                ap_ok = ApPermits<kAccess>(l1_pte.section.ap, is_user_mode);
            }
            if (!ap_ok) {
                RaiseAbort(p, ArmFaultStatus::kPermissionSection, kIsWrite);
                return nullptr;
            }
            effective_address = (l1_pte.section.section_base << 20) | (p & 0x000FFFFFu);
            tlb_slot = &slot;
            goto tlb_hit;
        }
        case 1: {
            if ((p >> 16) != slot.virtual_address) break;
            if (processor_config_->HasCp15V7()) {
                LOG(Caution, "MMU TLB hit: v6+ L2 large page not implemented "
                        "(va=0x%08X pte=0x%08X).\n", p, cached_pte.word);
                CerfFatalExit(2);
            }
            const uint32_t ap = (cached_pte.large_page.aps >> ((p >> 13) & 6u)) & 3u;
            if (!ApPermits<kAccess>(ap, is_user_mode)) {
                RaiseAbort(p, ArmFaultStatus::kPermissionPage, kIsWrite);
                return nullptr;
            }
            effective_address = (cached_pte.large_page.large_page_base << 16) | (p & 0xFFFFu);
            tlb_slot = &slot;
            goto tlb_hit;
        }
        case 2: {
            /* Small page (4 KB) — VA[31:12]. v5 APs indexed by VA[11:10];
               v6+ uses single AP at bits[5:4]+bit[9] (WinCE NK
               vmarm.h:47, ksarm.h:77). */
            if ((p >> 12) != slot.virtual_address) break;
            bool ap_ok;
            if (processor_config_->HasCp15V7()) {
                const uint32_t ap = ((cached_pte.word >> 4) & 3u) |
                                    (((cached_pte.word >> 9) & 1u) << 2);
                ap_ok = ApPermitsV6<kAccess>(ap, is_user_mode);
            } else {
                const uint32_t ap = (cached_pte.small_page.aps >> ((p >> 9) & 6u)) & 3u;
                ap_ok = ApPermits<kAccess>(ap, is_user_mode);
            }
            if (!ap_ok) {
                RaiseAbort(p, ArmFaultStatus::kPermissionPage, kIsWrite);
                return nullptr;
            }
            effective_address = (cached_pte.small_page.small_page_base << 12) | (p & 0x0FFFu);
            tlb_slot = &slot;
            goto tlb_hit;
        }
        case 3: {
            /* Extended small page (1 KB) — v5-only, single AP. */
            if ((p >> 10) != slot.virtual_address) break;
            if (!ApPermits<kAccess>(cached_pte.extended_small_page.ap, is_user_mode)) {
                RaiseAbort(p, ArmFaultStatus::kPermissionPage, kIsWrite);
                return nullptr;
            }
            effective_address = (cached_pte.extended_small_page.extended_small_page_base << 10) | (p & 0x01FFu);
            tlb_slot = &slot;
            goto tlb_hit;
        }
        }
    }

    /* TLB miss — walk the in-RAM page table. */
    {
        const uint32_t ttbcr_n   = state_.ttbcr & 7u;
        const uint32_t ttbr0_mask = ~((1u << (14u - ttbcr_n)) - 1u);
        const bool use_ttbr1 = ttbcr_n != 0u &&
                               (p >> (32u - ttbcr_n)) != 0u;
        const uint32_t l1_base = use_ttbr1
            ? (state_.ttbr1 & 0xFFFFC000u)
            : (state_.translation_table_base.word & ttbr0_mask);
        const uint32_t l1_pa = l1_base | ((p >> 20) << 2);
        uint8_t* l1_host = memory_->TryTranslateWrite(l1_pa);
        if (!l1_host) {
            RaiseAbort(p, ArmFaultStatus::kExternalAbortTranslation1, kIsWrite);
            return nullptr;
        }
        ArmL1Pte l1_pte;
        l1_pte.word = *reinterpret_cast<uint32_t*>(l1_host);

        ArmTlbSlot& new_slot = tlb_unit->slots[tlb_unit->next_free_slot];

        switch (l1_pte.fault.type) {
        case ArmL1PteType::kFault:
            RaiseAbort(p, ArmFaultStatus::kTranslationSection, kIsWrite);
            return nullptr;

        case ArmL1PteType::kCoarse: {
            if (l1_pte.coarse.domain != 0u) {
                RaiseAbort(p, ArmFaultStatus::kDomainPage, kIsWrite);
                return nullptr;
            }
            const uint32_t l2_pa = (l1_pte.coarse.page_table_base << 10) | (((p >> 12) & 0xFFu) << 2);
            uint8_t* l2_host = memory_->TryTranslateWrite(l2_pa);
            if (!l2_host) {
                RaiseAbort(p, ArmFaultStatus::kExternalAbortTranslation2, kIsWrite);
                return nullptr;
            }
            ArmL2Pte l2_pte;
            l2_pte.word = *reinterpret_cast<uint32_t*>(l2_host);

            switch (l2_pte.fault.type) {
            case 0:
                RaiseAbort(p, ArmFaultStatus::kTranslationPage, kIsWrite);
                return nullptr;
            case 3:
                /* WinCE NK ksarm.h:72 — ARMV6_MMU_PTL2_SMALL_XN = 1<<0,
                   so PTL2_SMALL_PAGE|XN = 2|1 = 3. ksarm.h:85 —
                   PREARMV6_MMU_PTL2_SMALL_XN = 0 (no XN, type=3 is fault). */
                if (!processor_config_->HasCp15V7()) {
                    RaiseAbort(p, ArmFaultStatus::kTranslationPage, kIsWrite);
                    return nullptr;
                }
                if constexpr (kAccess == ArmMmuAccess::kExecute) {
                    RaiseAbort(p, ArmFaultStatus::kPermissionPage, kIsWrite);
                    return nullptr;
                } else {
                    /* Clear XN so TLB-hit fast path decodes as 4 KB small
                       page (type=2), not legacy 1 KB extended small (type=3). */
                    l2_pte.word &= ~1u;
                }
                [[fallthrough]];
            case 2: {
                bool ap_ok;
                if (processor_config_->HasCp15V7()) {
                    const uint32_t ap = ((l2_pte.word >> 4) & 3u) |
                                        (((l2_pte.word >> 9) & 1u) << 2);
                    ap_ok = ApPermitsV6<kAccess>(ap, is_user_mode);
                } else {
                    const uint32_t ap = (l2_pte.small_page.aps >> ((p >> 9) & 6u)) & 3u;
                    ap_ok = ApPermits<kAccess>(ap, is_user_mode);
                }
                if (!ap_ok) {
                    RaiseAbort(p, ArmFaultStatus::kPermissionPage, kIsWrite);
                    return nullptr;
                }
                new_slot.pte             = l2_pte.word;
                new_slot.virtual_address = p >> 12;
                effective_address        = (l2_pte.small_page.small_page_base << 12) | (p & 0x0FFFu);
                break;
            }
            case 1: {
                if (processor_config_->HasCp15V7()) {
                    LOG(Caution, "MMU walk: v6+ L2 large page not implemented "
                            "(va=0x%08X L2_pte=0x%08X) — AP[2] bit position "
                            "unverified in available references.\n",
                        p, l2_pte.word);
                    CerfFatalExit(2);
                }
                const uint32_t ap = (l2_pte.large_page.aps >> ((p >> 13) & 6u)) & 3u;
                if (!ApPermits<kAccess>(ap, is_user_mode)) {
                    RaiseAbort(p, ArmFaultStatus::kPermissionPage, kIsWrite);
                    return nullptr;
                }
                new_slot.pte             = l2_pte.word;
                new_slot.virtual_address = p >> 16;
                effective_address        = (l2_pte.large_page.large_page_base << 16) | (p & 0xFFFFu);
                break;
            }
            }
            break;
        }

        case ArmL1PteType::kSection: {
            if (l1_pte.section.domain != 0u) {
                RaiseAbort(p, ArmFaultStatus::kDomainSection, kIsWrite);
                return nullptr;
            }
            bool ap_ok;
            uint32_t v7_ap = 0;
            if (processor_config_->HasCp15V7()) {
                /* WinCE NK ksarm.h:76 ARMV6_MMU_PTL1_KR0=0x8400 — bits[11:10]=AP[1:0],
                   bit[15]=APX. Subpage AP layout is v5-only. */
                v7_ap = ((l1_pte.word >> 10) & 3u) |
                        (((l1_pte.word >> 15) & 1u) << 2);
                ap_ok = ApPermitsV6<kAccess>(v7_ap, is_user_mode);
            } else {
                ap_ok = ApPermits<kAccess>(l1_pte.section.ap, is_user_mode);
            }
            if (!ap_ok) {
                LOG(Caution, "MMU walk: L1 section permission denied "
                        "va=0x%08X L1_pte=0x%08X v7_ap=%u access=%u user=%u\n",
                    p, l1_pte.word, v7_ap,
                    static_cast<unsigned>(kAccess),
                    static_cast<unsigned>(is_user_mode));
                RaiseAbort(p, ArmFaultStatus::kPermissionSection, kIsWrite);
                return nullptr;
            }
            /* PTEType=0 in stored TLB word marks the slot as a cached section. */
            new_slot.pte             = l1_pte.word & ~static_cast<uint32_t>(3);
            new_slot.virtual_address = p >> 20;
            effective_address        = (l1_pte.section.section_base << 20) | (p & 0x000FFFFFu);
            break;
        }

        case ArmL1PteType::kFine: {
            if (processor_config_->HasCp15V7()) {
                LOG(Caution, "MMU walk: v7 L1 type=3 (reserved) "
                        "(va=0x%08X L1_pa=0x%08X L1_pte=0x%08X).\n",
                    p, l1_pa, l1_pte.word);
                CerfFatalExit(2);
            }
            if (l1_pte.fine.domain != 0u) {
                RaiseAbort(p, ArmFaultStatus::kDomainPage, kIsWrite);
                return nullptr;
            }
            const uint32_t l2_pa = (l1_pte.fine.page_table_base << 14) | (((p >> 10) & 0x3FFu) << 2);
            uint8_t* l2_host = memory_->TryTranslateWrite(l2_pa);
            if (!l2_host) {
                RaiseAbort(p, ArmFaultStatus::kExternalAbortTranslation2, kIsWrite);
                return nullptr;
            }
            ArmL2Pte l2_pte;
            l2_pte.word = *reinterpret_cast<uint32_t*>(l2_host);

            switch (l2_pte.fault.type) {
            case 0:
                RaiseAbort(p, ArmFaultStatus::kTranslationPage, kIsWrite);
                return nullptr;
            case 1: {
                const uint32_t ap = (l2_pte.large_page.aps >> ((p >> 13) & 6u)) & 3u;
                if (!ApPermits<kAccess>(ap, is_user_mode)) {
                    RaiseAbort(p, ArmFaultStatus::kPermissionPage, kIsWrite);
                    return nullptr;
                }
                new_slot.pte             = l2_pte.word;
                new_slot.virtual_address = p >> 16;
                effective_address        = (l2_pte.large_page.large_page_base << 16) | (p & 0xFFFFu);
                break;
            }
            case 2: {
                const uint32_t ap = (l2_pte.small_page.aps >> ((p >> 9) & 6u)) & 3u;
                if (!ApPermits<kAccess>(ap, is_user_mode)) {
                    RaiseAbort(p, ArmFaultStatus::kPermissionPage, kIsWrite);
                    return nullptr;
                }
                new_slot.pte             = l2_pte.word;
                new_slot.virtual_address = p >> 12;
                effective_address        = (l2_pte.small_page.small_page_base << 12) | (p & 0x0FFFu);
                break;
            }
            case 3: {
                if (!ApPermits<kAccess>(l2_pte.extended_small_page.ap, is_user_mode)) {
                    RaiseAbort(p, ArmFaultStatus::kPermissionPage, kIsWrite);
                    return nullptr;
                }
                new_slot.pte             = l2_pte.word;
                new_slot.virtual_address = p >> 10;
                effective_address        = (l2_pte.extended_small_page.extended_small_page_base << 10) | (p & 0x01FFu);
                break;
            }
            }
            break;
        }
        }

        new_slot.valid       = true;
        tlb_slot             = &new_slot;
        *tlb_index_hint      = tlb_unit->next_free_slot;
        tlb_unit->next_free_slot = static_cast<int8_t>((tlb_unit->next_free_slot + 1) % kArmTlbSlotCount);

        if constexpr (kAccess == ArmMmuAccess::kWrite) {
            uint8_t* host_ptr = memory_->TryTranslateWrite(effective_address);
            if (host_ptr) {
                tlb_slot->host_adjust =
                    reinterpret_cast<uintptr_t>(host_ptr) - effective_address;
                return host_ptr;
            }
            tlb_slot->host_adjust = 0;
            SetIoPending(effective_address);
            return nullptr;
        } else {
            uint8_t* ram_host = memory_->TryTranslateWrite(effective_address);
            if (ram_host) {
                tlb_slot->host_adjust =
                    reinterpret_cast<uintptr_t>(ram_host) - effective_address;
                return ram_host;
            }
            uint8_t* flash_host = memory_->TryTranslate(effective_address);
            if (flash_host) {
                tlb_slot->host_adjust = 0;
                return flash_host;
            }
            tlb_slot->host_adjust = 0;
            SetIoPending(effective_address);
            return nullptr;
        }
    }

tlb_hit:
    *tlb_index_hint = idx;
    if (tlb_slot->host_adjust) {
        return reinterpret_cast<uint8_t*>(effective_address + tlb_slot->host_adjust);
    }
    if constexpr (kAccess != ArmMmuAccess::kWrite) {
        uint8_t* flash_host = memory_->TryTranslate(effective_address);
        if (flash_host) {
            return flash_host;
        }
    }
    SetIoPending(effective_address);
    return nullptr;
}

uint8_t* ArmMmu::TranslateRead(ArmCpuState* cpu_state, uint32_t va, int8_t* tlb_index_hint) {
    return MapGuestVirtualToHost<ArmMmuAccess::kRead>(cpu_state, va, tlb_index_hint);
}

uint8_t* ArmMmu::TranslateWrite(ArmCpuState* cpu_state, uint32_t va, int8_t* tlb_index_hint) {
    return MapGuestVirtualToHost<ArmMmuAccess::kWrite>(cpu_state, va, tlb_index_hint);
}

uint8_t* ArmMmu::TranslateReadWrite(ArmCpuState* cpu_state, uint32_t va, int8_t* tlb_index_hint) {
    return MapGuestVirtualToHost<ArmMmuAccess::kReadWrite>(cpu_state, va, tlb_index_hint);
}

uint8_t* ArmMmu::TranslateExecute(ArmCpuState* cpu_state, uint32_t va, int8_t* tlb_index_hint) {
    return MapGuestVirtualToHost<ArmMmuAccess::kExecute>(cpu_state, va, tlb_index_hint);
}
