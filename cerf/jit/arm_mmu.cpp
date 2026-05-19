#include "arm_mmu.h"

#include "../core/cerf_emulator.h"
#include "../cpu/arm_processor_config.h"
#include "../cpu/emulated_memory.h"
#include "arm_pte.h"
#include "arm_tlb_ops.h"

REGISTER_SERVICE(ArmMmu);

ArmMmu::~ArmMmu() = default;

void ArmMmu::OnReady() {
    memory_           = &emu_.Get<EmulatedMemory>();
    processor_config_ = &emu_.Get<ArmProcessorConfig>();

    ArmTlbFlushAll(&state_.data_tlb);
    ArmTlbFlushAll(&state_.instruction_tlb);
}

uint32_t __fastcall ArmMmu::CcsidrLookupHelper(ArmMmu* mmu) {
    /* cp15 c0 op1=1 CRm=0 op2=0 (CCSIDR), indexed by CSSELR.
       Reference: QEMU helper.c:942-947, cpu.h:1131-1134. */
    return mmu->emu_.Get<ArmProcessorConfig>().Ccsidr(mmu->state_.cssel_register);
}

void ArmMmu::RaiseAbort(uint32_t va, uint32_t fault_status, bool is_write) {
    state_.fault_status.bits.status = fault_status;
    state_.fault_status.bits.d      = 0;
    state_.fault_status.bits.x      = 1;
    state_.fault_status.bits.wnr    = is_write ? 1u : 0u;
    state_.fault_address             = va;
}

void ArmMmu::SetIoPending(uint32_t pa) {
    if (pa == 0u) {
        /* Encode PA-0 as (4, -4) so the io_pa sentinel is naturally
           aligned to every access size (byte/halfword/word) — the
           JIT-side alignment check tests low bits of EAX(=io_pa);
           a sentinel of 1 would falsely fail. Real PA = 4 + (-4) = 0. */
        io_pending_address_        = 4u;
        io_pending_address_adjust_ = static_cast<uint32_t>(-4);
    } else {
        io_pending_address_        = pa;
        io_pending_address_adjust_ = 0u;
    }
}


std::optional<uint8_t*> ArmMmu::PeekDataTlb(uint32_t va) const {
    /* Slot scan matches MapGuestVirtualToHost's per-PTE-type shift
       decode at lines 89-140 — section→20, large→16, small→12,
       extended-small→10 — except this peek never mutates TLB state,
       never raises an abort, never sets io_pending_address_. */
    for (size_t i = 0; i < kArmTlbSlotCount; ++i) {
        const ArmTlbSlot& slot = state_.data_tlb.slots[i];
        if (!slot.valid) continue;

        ArmL2Pte cached_pte;
        cached_pte.word = slot.pte;

        /* host_adjust is stored as (host_ptr - PA); using (VA + host_adjust)
           is wrong by (VA - PA) for non-identity kernel mappings. */
        uint32_t pa;
        switch (cached_pte.fault.type) {
        case 0:  /* section (1 MB) */
            if ((va >> 20) != slot.virtual_address) continue;
            pa = (cached_pte.word & 0xFFF00000u) | (va & 0x000FFFFFu);
            break;
        case 1:  /* large page (64 KB) */
            if ((va >> 16) != slot.virtual_address) continue;
            pa = (cached_pte.large_page.large_page_base << 16) | (va & 0xFFFFu);
            break;
        case 2:  /* small page (4 KB) */
            if ((va >> 12) != slot.virtual_address) continue;
            pa = (cached_pte.small_page.small_page_base << 12) | (va & 0x0FFFu);
            break;
        case 3:  /* extended small page (1 KB) */
            if ((va >> 10) != slot.virtual_address) continue;
            pa = (cached_pte.extended_small_page.extended_small_page_base << 10)
                 | (va & 0x01FFu);
            break;
        default:
            continue;
        }

        /* host_adjust == 0 marks a peripheral PA — no host mapping
           exists, dereference would be wrong. Treat as "not
           peekable" so trace handlers get nullopt rather than a
           bogus host pointer. */
        if (slot.host_adjust == 0) return std::nullopt;

        return reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(pa) + slot.host_adjust);
    }
    return std::nullopt;
}
