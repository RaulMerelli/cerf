#include "imx6_vivante_mem.h"

namespace imx6_vivante {

bool VivanteMem::TranslateMmuv1(uint32_t gpu_addr, MmuClient client,
                                uint32_t& phys) const {
    if (!Mmuv1LooksConfigured(client))
        return false;

    if (gpu_addr < kMmuv1GpuMemStart) {
        const uint32_t memory_base =
            s_.regs_[Mmuv1MemoryBaseRegister(client) >> 2];
        phys = memory_base + gpu_addr;
        return true;
    }

    /* MMUv1 indexes a 2 MiB flat table with GPU VA[30:12].  PTEs are
       raw aligned physical page addresses, with no MMUv2 permission bits.
       The KTP400 WinCE allocator uses only its first 1 MiB.  References:
       etnaviv_iommu.c etnaviv_iommuv1_map and libGALCore.dll IDA
       sub_EF0086A8/sub_EF0058BC/sub_EF00AC34. */
    const uint32_t page_index =
        (gpu_addr - kMmuv1GpuMemStart) >> 12;
    const uint32_t table_base =
        s_.regs_[Mmuv1PageTableRegister(client) >> 2];
    uint32_t pte = 0u;
    if (!ReadPhysicalU32(table_base + page_index * 4u, pte))
        return false;
    phys = (pte & kMmuv1PageMask) | (gpu_addr & ~kMmuv1PageMask);
    return true;
}

uint32_t VivanteMem::ActiveMtlbBase(MmuClient client) const {
    const uint32_t config = s_.regs_[kMmuv2Configuration >> 2];
    const uint32_t base_mask = (config & kMmuv2Mode1k) != 0u
        ? 0xFFFFFC00u : 0xFFFFF000u;
    const uint32_t configured = config & base_mask;
    if (configured != 0u)
        return configured;

    /* Legacy MC client tables remain available when CONFIGURATION.ADDRESS
       is zero. MODE1_K accepts 1 KiB alignment; MODE4_K ignores bits
       11:10 and retains the original 4 KiB alignment requirement. */
    static constexpr uint32_t kClientTable[] = {
        0x400u, /* FE */
        0x404u, /* TX */
        0x408u, /* PE color */
        0x410u, /* RA */
    };
    const uint32_t index = static_cast<uint32_t>(client);
    return s_.regs_[kClientTable[index] >> 2] & base_mask;
}

bool VivanteMem::WalkMmuv2(uint32_t gpu_addr, bool write, MmuClient client,
                           MmuWalkResult& walk) const {
    walk = MmuWalkResult{};
    if (!MmuLooksConfigured())
        return false;

    walk.configuration = s_.regs_[kMmuv2Configuration >> 2];
    walk.mtlb_base = ActiveMtlbBase(client);
    if (walk.mtlb_base == 0u) {
        ReportMmuFault(client, MmuException::SlaveNotPresent, gpu_addr);
        return false;
    }

    const bool mode1k = (walk.configuration & kMmuv2Mode1k) != 0u;
    const uint32_t mtlb_shift = Mmuv2MtlbShift(walk.configuration);
    const uint32_t mtlb_index = gpu_addr >> mtlb_shift;
    walk.mtlb_entry_addr = walk.mtlb_base + mtlb_index * 4u;
    if (!ReadPhysicalU32(walk.mtlb_entry_addr, walk.mtlb_desc)) {
        ReportMmuFault(client, MmuException::OutOfBound, gpu_addr);
        return false;
    }
    if ((walk.mtlb_desc & kMmuv2PtePresent) == 0u ||
        (walk.mtlb_desc & kMmuv2PteException) != 0u) {
        ReportMmuFault(client, MmuException::SlaveNotPresent, gpu_addr);
        return false;
    }

    const uint32_t page_type =
        (walk.mtlb_desc & kMmuv2MtlbPageSizeMask) >> 2;
    /* MODE4_K has only 4 KiB and 64 KiB leaves.  MODE1_K adds
       1 MiB and 16 MiB as documented by state_hi.xml. */
    if (!mode1k && page_type > 1u) {
        ReportMmuFault(client, MmuException::OutOfBound, gpu_addr);
        return false;
    }
    walk.page_shift = Mmuv2PageShift(page_type);

    uint32_t stlb_index = 0u;
    if (mode1k && page_type == 3u) {
        /* In 32-bit MODE1_K the proprietary Vivante allocator groups 16
           consecutive MTLB entries onto one 16-entry STLB.  The low four
           bits of the selected MTLB index therefore select the 16 MiB PTE. */
        stlb_index = mtlb_index & 0xFu;
    } else {
        const uint32_t stlb_bits = mtlb_shift - walk.page_shift;
        if (stlb_bits != 0u)
            stlb_index = (gpu_addr >> walk.page_shift) &
                         ((1u << stlb_bits) - 1u);
    }

    const uint32_t stlb_base =
        walk.mtlb_desc & kMmuv2StlbAddressMask;
    walk.pte_entry_addr = stlb_base + stlb_index * 4u;
    if (!ReadPhysicalU32(walk.pte_entry_addr, walk.pte)) {
        ReportMmuFault(client, MmuException::OutOfBound, gpu_addr);
        return false;
    }
    if ((walk.pte & kMmuv2PtePresent) == 0u ||
        (walk.pte & kMmuv2PteException) != 0u) {
        ReportMmuFault(client, MmuException::PageNotPresent, gpu_addr);
        return false;
    }
    if (write && (walk.pte & kMmuv2PteWriteable) == 0u) {
        ReportMmuFault(client, MmuException::WriteViolation, gpu_addr);
        return false;
    }

    const uint32_t page_mask = (1u << walk.page_shift) - 1u;
    walk.phys = (walk.pte & ~page_mask) | (gpu_addr & page_mask);
    return true;
}

bool VivanteMem::TranslationCacheMatches(const TranslationPageCache& cache,
                                         uint32_t gpu_addr, MmuClient client,
                                         bool write) const {
    const uint32_t page = gpu_addr & ~0xFFFu;
    if (cache.gpu_page != page ||
        (write ? cache.write_page == nullptr : cache.read_page == nullptr))
        return false;
    const uint32_t configuration = s_.regs_[kMmuv2Configuration >> 2];
    if (cache.configuration != configuration)
        return false;
    const uint32_t mtlb_base = ActiveMtlbBase(client);
    if (mtlb_base == 0u || mtlb_base != cache.mtlb_base)
        return false;
    uint32_t mtlb_desc = 0u;
    uint32_t pte = 0u;
    if (!ReadPhysicalU32(cache.mtlb_entry_addr, mtlb_desc) ||
        !ReadPhysicalU32(cache.pte_entry_addr, pte))
        return false;
    if (mtlb_desc != cache.mtlb_desc || pte != cache.pte ||
        (mtlb_desc & kMmuv2PtePresent) == 0u ||
        (mtlb_desc & kMmuv2PteException) != 0u ||
        (pte & kMmuv2PtePresent) == 0u ||
        (pte & kMmuv2PteException) != 0u ||
        (write && (pte & kMmuv2PteWriteable) == 0u))
        return false;
    return true;
}

void VivanteMem::FillTranslationCache(TranslationPageCache& cache,
                                      uint32_t gpu_addr,
                                      const MmuWalkResult& walk,
                                      const uint8_t* read_page,
                                      uint8_t* write_page) const {
    cache = TranslationPageCache{};
    cache.gpu_page = gpu_addr & ~0xFFFu;
    cache.configuration = walk.configuration;
    cache.mtlb_base = walk.mtlb_base;
    cache.mtlb_entry_addr = walk.mtlb_entry_addr;
    cache.mtlb_desc = walk.mtlb_desc;
    cache.pte_entry_addr = walk.pte_entry_addr;
    cache.pte = walk.pte;
    cache.read_page = read_page;
    cache.write_page = write_page;
}

void VivanteMem::ReportMmuFault(MmuClient client, MmuException exception,
                                uint32_t gpu_addr) const {
    const uint32_t slot = static_cast<uint32_t>(client) & 3u;
    const uint32_t shift = slot * 4u;
    const uint32_t field_mask = 0xFu << shift;
    uint32_t& status = s_.regs_[kMmuv2Status >> 2];
    status = (status & ~field_mask) |
             ((static_cast<uint32_t>(exception) & 0xFu) << shift);
    s_.regs_[(kMmuv2ExceptionAddress >> 2) + slot] = gpu_addr;
    s_.intr_status_ |= kMmuv2Interrupt;
    UpdateIrq();
}

const uint8_t* VivanteMem::TranslateMmuSafeRead(uint32_t fault_addr) const {
    if (!s_.mmu_safe_address_written_)
        return nullptr;
    const uint32_t safe_base =
        s_.regs_[kMmuv2SafeAddress >> 2] & ~0x3Fu;
    auto& mem = emu_.Get<EmulatedMemory>();
    /* Preserve the low six bits so the entire documented 64-byte safe
       window is addressable, exactly as a redirected bus transaction. */
    return mem.TryTranslate(safe_base | (fault_addr & 0x3Fu));
}

const uint8_t* VivanteMem::TranslateGpuToHost(
    uint32_t gpu_addr, MmuClient client) const {
    auto& mem = emu_.Get<EmulatedMemory>();
    if (MmuLooksConfigured()) {
        const uint32_t slot = static_cast<uint32_t>(client) & 3u;
        const uint32_t offset = gpu_addr & 0xFFFu;
        TranslationPageCache& cache = translation_cache_[slot];
        if (TranslationCacheMatches(cache, gpu_addr, client, false))
            return cache.read_page + offset;

        MmuWalkResult walk{};
        if (!WalkMmuv2(gpu_addr, false, client, walk)) {
            cache.gpu_page = 0xFFFFFFFFu;
            cache.read_page = nullptr;
            cache.write_page = nullptr;
            return TranslateMmuSafeRead(gpu_addr);
        }
        const uint32_t phys_page = walk.phys & ~0xFFFu;
        if (auto* mapped_page = mem.TryTranslate(phys_page)) {
            FillTranslationCache(cache, gpu_addr, walk, mapped_page,
                                 mem.TryTranslateWrite(phys_page));
            return mapped_page + offset;
        }
        ReportMmuFault(client, MmuException::OutOfBound, gpu_addr);
        cache.gpu_page = 0xFFFFFFFFu;
        cache.read_page = nullptr;
        cache.write_page = nullptr;
        return TranslateMmuSafeRead(gpu_addr);
    }

    uint32_t mmuv1_phys = 0u;
    if (TranslateMmuv1(gpu_addr, client, mmuv1_phys))
        return mem.TryTranslate(mmuv1_phys);

    /* Before MMUv2 is enabled the setup stream and boot surfaces can be
       physical addresses or guest VAs.  These compatibility paths are
       deliberately unavailable after CONTROL.ENABLE. */
    if (auto* arm_mmu = emu_.TryGet<ArmMmuProbe>()) {
        uint32_t pa = 0u;
        if (arm_mmu->PeekVaToPa(gpu_addr, &pa)) {
            if (auto* mapped = mem.TryTranslate(pa))
                return mapped;
        }
    }
    return mem.TryTranslate(gpu_addr);
}

uint8_t* VivanteMem::TranslateGpuToHostWrite(
    uint32_t gpu_addr, MmuClient client) const {
    auto& mem = emu_.Get<EmulatedMemory>();
    if (MmuLooksConfigured()) {
        const uint32_t slot = static_cast<uint32_t>(client) & 3u;
        const uint32_t offset = gpu_addr & 0xFFFu;
        TranslationPageCache& cache = translation_cache_[slot];
        if (TranslationCacheMatches(cache, gpu_addr, client, true))
            return cache.write_page + offset;

        MmuWalkResult walk{};
        if (!WalkMmuv2(gpu_addr, true, client, walk)) {
            /* Hardware redirects the transaction to SAFE_ADDRESS with
               write-enable forced to zero. Returning no writable pointer
               preserves that externally visible write-suppression. */
            cache.gpu_page = 0xFFFFFFFFu;
            cache.read_page = nullptr;
            cache.write_page = nullptr;
            return nullptr;
        }
        const uint32_t phys_page = walk.phys & ~0xFFFu;
        if (auto* mapped_page = mem.TryTranslateWrite(phys_page)) {
            FillTranslationCache(cache, gpu_addr, walk,
                                 mem.TryTranslate(phys_page), mapped_page);
            return mapped_page + offset;
        }
        ReportMmuFault(client, MmuException::OutOfBound, gpu_addr);
        cache.gpu_page = 0xFFFFFFFFu;
        cache.read_page = nullptr;
        cache.write_page = nullptr;
        return nullptr;
    }

    uint32_t mmuv1_phys = 0u;
    if (TranslateMmuv1(gpu_addr, client, mmuv1_phys))
        return mem.TryTranslateWrite(mmuv1_phys);

    if (auto* arm_mmu = emu_.TryGet<ArmMmuProbe>()) {
        uint32_t pa = 0u;
        if (arm_mmu->PeekVaToPa(gpu_addr, &pa)) {
            if (auto* mapped = mem.TryTranslateWrite(pa))
                return mapped;
        }
    }
    return mem.TryTranslateWrite(gpu_addr);
}

bool VivanteMem::TranslateGpuViaMmu(uint32_t gpu_addr, bool write,
                                    MmuClient client, uint32_t& phys) const {
    MmuWalkResult walk{};
    if (WalkMmuv2(gpu_addr, write, client, walk)) {
        phys = walk.phys;
        return true;
    }
    if (!MmuLooksConfigured())
        return TranslateMmuv1(gpu_addr, client, phys);
    return false;
}

}  // namespace imx6_vivante
