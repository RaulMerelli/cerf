#include "arm_mmu.h"

#include <cstring>

#include "../../boards/board_context.h"
#include "../../boards/page_table_builder.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "../../cpu/emulated_memory.h"
#include "arm_cpu.h"
#include "arm_pte.h"
#include "arm_tlb_ops.h"
#include "../../state/state_stream.h"

REGISTER_SERVICE(ArmMmu);

bool ArmMmu::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

ArmMmu::~ArmMmu() = default;

void ArmMmu::SaveState(StateWriter& w) {
    w.Write(state_.control_register);
    w.Write(state_.aux_control_register);
    w.Write(state_.translation_table_base);
    w.Write(state_.domain_access_control);
    w.Write(state_.fault_status);
    w.Write(state_.fault_address);
    w.Write(state_.ifsr);
    w.Write(state_.ifar);
    w.Write(state_.process_id);
    w.Write(state_.coprocessor_access);
    w.Write(state_.cssel_register);
    w.Write(state_.ttbr1);
    w.Write(state_.ttbcr);
    w.Write(state_.prrr);
    w.Write(state_.nmrr);
    w.Write(state_.contextidr);
    w.Write(state_.tpidrurw);
    w.Write(state_.tpidruro);
    w.Write(state_.tpidrprw);
    w.Write(state_.l2_aux_control);
}

void ArmMmu::RestoreState(StateReader& r) {
    r.Read(state_.control_register);
    r.Read(state_.aux_control_register);
    r.Read(state_.translation_table_base);
    r.Read(state_.domain_access_control);
    r.Read(state_.fault_status);
    r.Read(state_.fault_address);
    r.Read(state_.ifsr);
    r.Read(state_.ifar);
    r.Read(state_.process_id);
    r.Read(state_.coprocessor_access);
    r.Read(state_.cssel_register);
    r.Read(state_.ttbr1);
    r.Read(state_.ttbcr);
    r.Read(state_.prrr);
    r.Read(state_.nmrr);
    r.Read(state_.contextidr);
    r.Read(state_.tpidrurw);
    r.Read(state_.tpidruro);
    r.Read(state_.tpidrprw);
    r.Read(state_.l2_aux_control);
    /* Restored TTBR0/process_id/contextidr differ from the live TLBs'
       context; a stale entry would return the prior context's PA. */
    ArmTlbFlushAll(&state_.data_tlb);
    ArmTlbFlushAll(&state_.instruction_tlb);
}

void ArmMmu::OnReady() {
    memory_           = &emu_.Get<EmulatedMemory>();
    processor_config_ = &emu_.Get<ArmProcessorConfig>();

    /* SMC word-bitmap spans the board's DRAM PA extent only: code is
       writable (hence self-modifiable) solely in DRAM, so a write outside
       this range can never invalidate a translation. */
    uint32_t dram_min = 0xFFFFFFFFu;
    uint32_t dram_max = 0u;
    for (const auto& r : emu_.Get<PageTableBuilder>().CachedDramRegions()) {
        if (r.pa_base < dram_min)          dram_min = r.pa_base;
        if (r.pa_base + r.size > dram_max) dram_max = r.pa_base + r.size;
    }
    state_.code_word_base         = dram_min;
    state_.code_word_top          = dram_max;
    state_.code_word_bitmap_bytes =
        (dram_max > dram_min) ? (((dram_max - dram_min) >> 2) + 7u) / 8u : 0u;

    code_xlat_bitmap_storage_.assign(state_.code_word_bitmap_bytes, 0u);
    state_.code_xlat_bitmap = code_xlat_bitmap_storage_.data();

    state_.code_page_dirty_bytes =
        (dram_max > dram_min) ? (((dram_max - dram_min) >> 12) + 7u) / 8u : 0u;
    code_page_dirty_storage_.assign(state_.code_page_dirty_bytes, 0u);
    state_.code_page_dirty = code_page_dirty_storage_.data();

    ArmTlbFlushAll(&state_.data_tlb);
    ArmTlbFlushAll(&state_.instruction_tlb);
}

uint32_t __fastcall ArmMmu::CcsidrLookupHelper(ArmMmu* mmu) {
    /* cp15 c0 op1=1 CRm=0 op2=0 (CCSIDR), indexed by CSSELR.
       Reference: QEMU helper.c:942-947, cpu.h:1131-1134. */
    return mmu->emu_.Get<ArmProcessorConfig>().Ccsidr(mmu->state_.cssel_register);
}

bool ArmMmu::IsReadOnlyBacked(uint32_t pa) {
    /* TryTranslateWrite returns nullptr for a backed region exactly when its
       host protection is PAGE_READONLY / PAGE_EXECUTE_READ (EmulatedMemory
       sources page_protect from PageTableBuilder's ROM/flash declarations);
       a non-null TryTranslate confirms the PA is backed at all. */
    return memory_->TryTranslate(pa) != nullptr &&
           memory_->TryTranslateWrite(pa) == nullptr;
}

/* ARM DDI 0406C.c Table B3-26 (p. B3-1418): Prefetch Abort faults update
   IFSR+IFAR only, Data Abort faults DFSR+DFAR only. B4.1.52 + Table B3-27:
   DFSR.Domain is UNKNOWN on alignment and first-level faults, so 0 is
   permitted there. */
void ArmMmu::RaiseAbort(uint32_t va, uint32_t fault_status, uint32_t domain,
                        ArmMmuAccess access) {
    if (access == ArmMmuAccess::kExecute) {
        if (processor_config_->HasCp15V6()) {
            /* B4.1.96: IFSR carries FS[3:0] (FS[4] is 0 for every Table
               B3-23 code CERF raises), no Domain, no WnR. */
            state_.ifsr = fault_status;
            state_.ifar = va;
            return;
        }
        /* ARM DDI 0406C.c D15.7.6 (p. D15-2623): the single ARMv4/v5 FSR
           "is updated on Prefetch Abort exceptions and Data Abort
           exceptions"; the FAR "is only updated with the MVA on Data
           Abort exceptions". FSR = Domain[7:4] + FS[3:0]. */
        ArmDfsr fsr{};
        fsr.bits.status      = fault_status;
        fsr.bits.domain      = domain;
        state_.fault_status  = fsr;
        return;
    }
    ArmDfsr dfsr{};
    dfsr.bits.status     = fault_status;
    dfsr.bits.domain     = domain;
    /* WnR is DFSR[11] (B4.1.52); D15.7.6 makes v4/v5 FSR bits[31:11]
       UNK/SBZP, so the write is permitted there too. */
    dfsr.bits.wnr        = (access == ArmMmuAccess::kWrite ||
                            access == ArmMmuAccess::kReadWrite) ? 1u : 0u;
    state_.fault_status  = dfsr;
    state_.fault_address = va;
}

void ArmMmu::RaiseAlignmentFault(uint32_t va, bool is_write) {
    ArmDfsr dfsr{};
    dfsr.bits.status     = ArmFaultStatus::kAlignment;
    dfsr.bits.wnr        = is_write ? 1u : 0u;
    state_.fault_status  = dfsr;
    state_.fault_address = va;
}

void __fastcall ArmMmu::AlignmentFaultReadHelper(uint32_t va, ArmMmu* mmu) {
    mmu->RaiseAlignmentFault(va, /*is_write=*/false);
}

void __fastcall ArmMmu::AlignmentFaultWriteHelper(uint32_t va, ArmMmu* mmu) {
    mmu->RaiseAlignmentFault(va, /*is_write=*/true);
}

uint32_t __cdecl ArmMmu::UnalignedHalfwordLoadHelper(ArmMmu* mmu, uint32_t va) {
    ArmCpuState* cs = mmu->emu_.Get<ArmCpu>().State();
    uint8_t b[2];
    if (!mmu->AccessPaged(cs, va,      &b[0], 1, /*is_load=*/true) ||
        !mmu->AccessPaged(cs, va + 1u, &b[1], 1, /*is_load=*/true)) {
        return 0xFFFFFFFFu;
    }
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8);
}

uint32_t __cdecl ArmMmu::UnalignedHalfwordStoreHelper(ArmMmu* mmu, uint32_t va,
                                                      uint32_t value) {
    ArmCpuState* cs = mmu->emu_.Get<ArmCpu>().State();
    uint8_t b[2] = { static_cast<uint8_t>(value),
                     static_cast<uint8_t>(value >> 8) };
    if (!mmu->AccessPaged(cs, va,      &b[0], 1, /*is_load=*/false) ||
        !mmu->AccessPaged(cs, va + 1u, &b[1], 1, /*is_load=*/false)) {
        return 0xFFFFFFFFu;
    }
    return 0u;
}

uint64_t __cdecl ArmMmu::UnalignedWordLoadHelper(ArmMmu* mmu, uint32_t va) {
    ArmCpuState* cs = mmu->emu_.Get<ArmCpu>().State();
    uint8_t b[4];
    for (uint32_t i = 0; i < 4u; ++i) {
        if (!mmu->AccessPaged(cs, va + i, &b[i], 1, /*is_load=*/true)) {
            return 0u;
        }
    }
    const uint32_t value = static_cast<uint32_t>(b[0]) |
                           (static_cast<uint32_t>(b[1]) << 8) |
                           (static_cast<uint32_t>(b[2]) << 16) |
                           (static_cast<uint32_t>(b[3]) << 24);
    return (1ull << 32) | value;
}

uint32_t __cdecl ArmMmu::UnalignedWordStoreHelper(ArmMmu* mmu, uint32_t va,
                                                  uint32_t value) {
    ArmCpuState* cs = mmu->emu_.Get<ArmCpu>().State();
    uint8_t b[4] = { static_cast<uint8_t>(value),
                     static_cast<uint8_t>(value >> 8),
                     static_cast<uint8_t>(value >> 16),
                     static_cast<uint8_t>(value >> 24) };
    for (uint32_t i = 0; i < 4u; ++i) {
        if (!mmu->AccessPaged(cs, va + i, &b[i], 1, /*is_load=*/false)) {
            return 0xFFFFFFFFu;
        }
    }
    return 0u;
}

void ArmMmu::SetIoPending(uint32_t pa) {
    if (pa == 0u) {
        LOG(Caution, "ArmMmu::SetIoPending: MMIO at PA 0 is not implemented "
                "(0 is the no-io-pending value).\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    io_pending_address_ = pa;
}

void ArmMmu::SetInjectionBand(uint32_t va_base, uint32_t pa_base, uint32_t size) {
    injection_band_va_   = va_base;
    injection_band_pa_   = pa_base;
    injection_band_size_ = size;
}

uint8_t* ArmMmu::ServeInjectionBand(uint32_t va, ArmMmuAccess access) {
    if (injection_band_size_ == 0u) return nullptr;
    const uint32_t off = va - injection_band_va_;
    if (off >= injection_band_size_) return nullptr;
    const uint32_t pa = injection_band_pa_ + off;
    const bool is_write = (access == ArmMmuAccess::kWrite ||
                           access == ArmMmuAccess::kReadWrite);
    uint8_t* host = is_write ? memory_->TryTranslateWrite(pa)
                             : memory_->TryTranslate(pa);
    if (!host) return nullptr;
    if (access == ArmMmuAccess::kExecute) last_exec_pa_ = pa;
    else                                  last_data_pa_ = pa;
    return host;
}


const ArmTlbEntry* ArmMmu::MatchDataTlb(uint32_t va, uint32_t* folded) const {
    uint32_t p = va;
    /* ARM DDI 0406C.c B3.19.2 FCSETranslate: va<31:25> == '0000000' folds
       to FCSEIDR.PID:va<24:0>, with no SCTLR.M term. */
    if ((p & 0xFE000000u) == 0u) {
        p |= state_.process_id;
    }
    *folded = p;
    const uint8_t current_asid = static_cast<uint8_t>(state_.contextidr & 0xFFu);
    const uint32_t base = ArmTlbSetBase(p);
    const int w = ArmTlbMatchWay(&state_.data_tlb, base, p & 0xFFFFF000u,
                                 current_asid, false);
    if (w < 0) return nullptr;
    return &state_.data_tlb.entries[base + static_cast<uint32_t>(w)];
}

std::optional<uint8_t*> ArmMmu::PeekDataTlb(uint32_t va) const {
    /* Diagnostic-only: never walks, never raises, never mutates TLB state.
       Returns a host pointer only on a direct-mapped fast-TLB hit. */
    uint32_t p = 0;
    const ArmTlbEntry* e = MatchDataTlb(va, &p);
    if (!e) return std::nullopt;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(p) + e->va_addend);
}

bool ArmMmu::ExecPageGlobal(uint32_t va) const {
    /* va is already FCSE-folded by the caller (JitCreateEntrypoints passes the
       decoded insn's actual_guest_address). Absent ⇒ false: claiming global for
       an evicted user (nG=1) page would route its block into the shared global
       tree where another process would execute it. */
    const uint8_t current_asid = static_cast<uint8_t>(state_.contextidr & 0xFFu);
    const uint32_t base = ArmTlbSetBase(va);
    const int w = ArmTlbMatchWay(&state_.instruction_tlb, base,
                                 va & 0xFFFFF000u, current_asid,
                                 /*need_write=*/false);
    return w >= 0 &&
           state_.instruction_tlb.entries[base + static_cast<uint32_t>(w)].global != 0u;
}

bool ArmMmu::AccessPaged(ArmCpuState* cpu_state, uint32_t va,
                         uint8_t* host_buf, uint32_t n, bool is_load) {
    for (uint32_t done = 0; done < n; ) {
        const uint32_t va_cur = va + done;
        uint8_t* host = is_load ? TranslateRead (cpu_state, va_cur)
                                : TranslateWrite(cpu_state, va_cur);
        if (host == nullptr) return false;
        /* ARM DDI 0406C.c Table D15-10 (p. D15-2609): a Tiny page maps 1 KB. */
        const uint32_t page_left = 0x400u - (va_cur & 0x3FFu);
        const uint32_t chunk = (n - done < page_left) ? (n - done) : page_left;
        if (is_load) std::memcpy(host_buf + done, host, chunk);
        else         std::memcpy(host, host_buf + done, chunk);
        done += chunk;
    }
    return true;
}
