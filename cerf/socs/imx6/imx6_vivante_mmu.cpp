#include "imx6_vivante_mmu.h"

#include "../../core/cerf_emulator.h"
#include "../../cpu/emulated_memory.h"
#include "imx6_gic.h"

#include <cstring>

namespace imx6_vivante {

void VivanteMmu::InvalidateTranslationCache() const {
    for (auto& cache : translation_cache_) cache = TranslationPageCache{};
}

bool VivanteMmu::LooksConfigured() const {
    return (state_.regs_[kMmuv2Control >> 2] & 1u) != 0u;
}

bool VivanteMmu::Mmuv1LooksConfigured(MmuClient client) const {
    return state_.regs_[Mmuv1PageTableRegister(client) >> 2] != 0u;
}

uint32_t VivanteMmu::Mmuv1PageTableRegister(MmuClient client) {
    static constexpr uint32_t registers[] = {
        kMmuv1FePageTable, kMmuv1TxPageTable,
        kMmuv1PePageTable, kMmuv1RaPageTable,
    };
    return registers[static_cast<uint32_t>(client)];
}

uint32_t VivanteMmu::Mmuv1MemoryBaseRegister(MmuClient client) {
    static constexpr uint32_t registers[] = {
        kMmuv1MemoryBaseFe, kMmuv1MemoryBaseTx,
        kMmuv1MemoryBasePe, kMmuv1MemoryBaseRa,
    };
    return registers[static_cast<uint32_t>(client)];
}

uint32_t VivanteMmu::MtlbShift(uint32_t configuration) {
    return (configuration & kMmuv2Mode1k) != 0u
        ? kMmuv2Mtlb1kShift : kMmuv2Mtlb4kShift;
}

uint32_t VivanteMmu::PageShift(uint32_t page_type) {
    static constexpr uint32_t shifts[4] = {12u, 16u, 20u, 24u};
    return shifts[page_type & 3u];
}

bool VivanteMmu::ReadPhysicalU32(uint32_t address, uint32_t& value) const {
    const uint8_t* entry = emu_.Get<EmulatedMemory>().TryTranslate(address);
    if (!entry) return false;
    std::memcpy(&value, entry, sizeof(value));
    return true;
}

void VivanteMmu::WriteConfiguration(uint32_t value) {
    InvalidateTranslationCache();
    uint32_t current = state_.regs_[kMmuv2Configuration >> 2];
    if ((value & (1u << 3)) == 0u)
        current = (current & ~1u) | (value & 1u);
    if ((value & (1u << 7)) == 0u) current &= ~(1u << 4);
    if ((value & (1u << 8)) == 0u)
        current = (current & ~0xFFFFFC00u) | (value & 0xFFFFFC00u);
    state_.regs_[kMmuv2Configuration >> 2] = current;
    const uint32_t index = kMmuv2Configuration >> 2;
    if (index < state_.state_.size()) state_.state_[index] = current;
}

void VivanteMmu::WriteSafeAddress(uint32_t value) {
    if (state_.mmu_safe_address_written_) return;
    state_.regs_[kMmuv2SafeAddress >> 2] = value & ~0x3Fu;
    state_.mmu_safe_address_written_ = true;
    const uint32_t index = kMmuv2SafeAddress >> 2;
    if (index < state_.state_.size()) state_.state_[index] = state_.regs_[index];
}

void VivanteMmu::Reset() {
    InvalidateTranslationCache();
    state_.regs_[kMmuv2SafeAddress >> 2] = 0u;
    state_.regs_[kMmuv2Configuration >> 2] = 0u;
    state_.regs_[kMmuv2Status >> 2] = 0u;
    state_.regs_[kMmuv2Control >> 2] = 0u;
    for (uint32_t i = 0u; i < 4u; ++i)
        state_.regs_[(kMmuv2ExceptionAddress >> 2) + i] = 0u;
    state_.mmu_safe_address_written_ = false;
    if ((kMmuv2Configuration >> 2) < state_.state_.size()) {
        state_.state_[kMmuv2SafeAddress >> 2] = 0u;
        state_.state_[kMmuv2Configuration >> 2] = 0u;
    }
    state_.intr_status_ &= ~kMmuv2Interrupt;
    UpdateIrq();
}

void VivanteMmu::UpdateIrq() const {
    const bool asserted = (state_.intr_status_ & state_.intr_enable_) != 0u;
    if (asserted == state_.irq_asserted_) return;
    state_.irq_asserted_ = asserted;
    auto& gic = emu_.Get<Imx6Gic>();
    if (asserted)
        gic.AssertSpi(irq_spi_);
    else
        gic.DeAssertSpi(irq_spi_);
}

bool VivanteMmu::TranslateMmuv1(uint32_t gpu_addr, MmuClient client, uint32_t& phys) const {
    if (!Mmuv1LooksConfigured(client)) return false;

    if (gpu_addr < kMmuv1GpuMemStart) {
        const uint32_t memory_base = state_.regs_[Mmuv1MemoryBaseRegister(client) >> 2];
        phys = memory_base + gpu_addr;
        return true;
    }

    /* Linux etnaviv_iommu.c::etnaviv_iommuv1_map indexes 4 KiB raw PA entries;
       hmi_ktp400_mobile_v13 libGALCore.dll sub_EF0086A8 uses the first 1 MiB. */
    const uint32_t page_index = (gpu_addr - kMmuv1GpuMemStart) >> 12;
    const uint32_t table_base = state_.regs_[Mmuv1PageTableRegister(client) >> 2];
    uint32_t pte = 0u;
    if (!ReadPhysicalU32(table_base + page_index * 4u, pte)) return false;
    phys = (pte & kMmuv1PageMask) | (gpu_addr & ~kMmuv1PageMask);
    return true;
}

uint32_t VivanteMmu::ActiveMtlbBase(MmuClient client) const {
    const uint32_t config = state_.regs_[kMmuv2Configuration >> 2];
    const uint32_t base_mask = (config & kMmuv2Mode1k) != 0u ? 0xFFFFFC00u : 0xFFFFF000u;
    const uint32_t configured = config & base_mask;
    if (configured != 0u) return configured;

    static constexpr uint32_t kClientTable[] = {
        0x400u,
        0x404u,
        0x408u,
        0x410u,
    };
    const uint32_t index = static_cast<uint32_t>(client);
    return state_.regs_[kClientTable[index] >> 2] & base_mask;
}

bool VivanteMmu::Walk(uint32_t gpu_addr, bool write, MmuClient client, WalkResult& walk) const {
    walk = WalkResult{};
    if (!LooksConfigured()) return false;

    walk.configuration = state_.regs_[kMmuv2Configuration >> 2];
    walk.mtlb_base = ActiveMtlbBase(client);
    if (walk.mtlb_base == 0u) {
        ReportFault(client, MmuException::SlaveNotPresent, gpu_addr);
        return false;
    }

    const bool mode1k = (walk.configuration & kMmuv2Mode1k) != 0u;
    const uint32_t mtlb_shift = MtlbShift(walk.configuration);
    const uint32_t mtlb_index = gpu_addr >> mtlb_shift;
    walk.mtlb_entry_address = walk.mtlb_base + mtlb_index * 4u;
    if (!ReadPhysicalU32(walk.mtlb_entry_address, walk.mtlb_descriptor)) {
        ReportFault(client, MmuException::OutOfBound, gpu_addr);
        return false;
    }
    if ((walk.mtlb_descriptor & kMmuv2PtePresent) == 0u || (walk.mtlb_descriptor & kMmuv2PteException) != 0u) {
        ReportFault(client, MmuException::SlaveNotPresent, gpu_addr);
        return false;
    }

    const uint32_t page_type = (walk.mtlb_descriptor & kMmuv2MtlbPageSizeMask) >> 2;
    if (!mode1k && page_type > 1u) {
        ReportFault(client, MmuException::OutOfBound, gpu_addr);
        return false;
    }
    walk.page_shift = PageShift(page_type);

    uint32_t stlb_index = 0u;
    if (mode1k && page_type == 3u) {
        stlb_index = mtlb_index & 0xFu;
    } else {
        const uint32_t stlb_bits = mtlb_shift - walk.page_shift;
        if (stlb_bits != 0u) stlb_index = (gpu_addr >> walk.page_shift) & ((1u << stlb_bits) - 1u);
    }

    const uint32_t stlb_base = walk.mtlb_descriptor & kMmuv2StlbAddressMask;
    walk.pte_entry_address = stlb_base + stlb_index * 4u;
    if (!ReadPhysicalU32(walk.pte_entry_address, walk.pte)) {
        ReportFault(client, MmuException::OutOfBound, gpu_addr);
        return false;
    }
    if ((walk.pte & kMmuv2PtePresent) == 0u || (walk.pte & kMmuv2PteException) != 0u) {
        ReportFault(client, MmuException::PageNotPresent, gpu_addr);
        return false;
    }
    if (write && (walk.pte & kMmuv2PteWriteable) == 0u) {
        ReportFault(client, MmuException::WriteViolation, gpu_addr);
        return false;
    }

    const uint32_t page_mask = (1u << walk.page_shift) - 1u;
    walk.physical = (walk.pte & ~page_mask) | (gpu_addr & page_mask);
    return true;
}

bool VivanteMmu::CacheMatches(const TranslationPageCache& cache, uint32_t gpu_addr, MmuClient client,
                                         bool write) const {
    const uint32_t page = gpu_addr & ~0xFFFu;
    if (cache.gpu_page != page || (write ? cache.write_page == nullptr : cache.read_page == nullptr)) return false;
    const uint32_t configuration = state_.regs_[kMmuv2Configuration >> 2];
    if (cache.configuration != configuration) return false;
    const uint32_t mtlb_base = ActiveMtlbBase(client);
    if (mtlb_base == 0u || mtlb_base != cache.mtlb_base) return false;
    uint32_t mtlb_desc = 0u;
    uint32_t pte = 0u;
    if (!ReadPhysicalU32(cache.mtlb_entry_address, mtlb_desc) || !ReadPhysicalU32(cache.pte_entry_address, pte)) return false;
    if (mtlb_desc != cache.mtlb_descriptor || pte != cache.pte || (mtlb_desc & kMmuv2PtePresent) == 0u ||
        (mtlb_desc & kMmuv2PteException) != 0u || (pte & kMmuv2PtePresent) == 0u || (pte & kMmuv2PteException) != 0u ||
        (write && (pte & kMmuv2PteWriteable) == 0u))
        return false;
    return true;
}

void VivanteMmu::FillCache(TranslationPageCache& cache, uint32_t gpu_addr, const WalkResult& walk,
                                      const uint8_t* read_page, uint8_t* write_page) const {
    cache = TranslationPageCache{};
    cache.gpu_page = gpu_addr & ~0xFFFu;
    cache.configuration = walk.configuration;
    cache.mtlb_base = walk.mtlb_base;
    cache.mtlb_entry_address = walk.mtlb_entry_address;
    cache.mtlb_descriptor = walk.mtlb_descriptor;
    cache.pte_entry_address = walk.pte_entry_address;
    cache.pte = walk.pte;
    cache.read_page = read_page;
    cache.write_page = write_page;
}

void VivanteMmu::ReportFault(MmuClient client, MmuException exception, uint32_t gpu_addr) const {
    const uint32_t slot = static_cast<uint32_t>(client) & 3u;
    const uint32_t shift = slot * 4u;
    const uint32_t field_mask = 0xFu << shift;
    uint32_t& status = state_.regs_[kMmuv2Status >> 2];
    status = (status & ~field_mask) | ((static_cast<uint32_t>(exception) & 0xFu) << shift);
    state_.regs_[(kMmuv2ExceptionAddress >> 2) + slot] = gpu_addr;
    state_.intr_status_ |= kMmuv2Interrupt;
    UpdateIrq();
}

const uint8_t* VivanteMmu::TranslateSafeRead(uint32_t fault_addr) const {
    if (!state_.mmu_safe_address_written_) return nullptr;
    const uint32_t safe_base = state_.regs_[kMmuv2SafeAddress >> 2] & ~0x3Fu;
    auto& mem = emu_.Get<EmulatedMemory>();
    return mem.TryTranslate(safe_base | (fault_addr & 0x3Fu));
}

const uint8_t* VivanteMmu::TranslateToHost(uint32_t gpu_addr, MmuClient client) const {
    auto& mem = emu_.Get<EmulatedMemory>();
    if (LooksConfigured()) {
        const uint32_t slot = static_cast<uint32_t>(client) & 3u;
        const uint32_t offset = gpu_addr & 0xFFFu;
        TranslationPageCache& cache = translation_cache_[slot];
        if (CacheMatches(cache, gpu_addr, client, false)) return cache.read_page + offset;

        WalkResult walk{};
        if (!Walk(gpu_addr, false, client, walk)) {
            cache.gpu_page = 0xFFFFFFFFu;
            cache.read_page = nullptr;
            cache.write_page = nullptr;
            return TranslateSafeRead(gpu_addr);
        }
        const uint32_t phys_page = walk.physical & ~0xFFFu;
        if (auto* mapped_page = mem.TryTranslate(phys_page)) {
            FillCache(cache, gpu_addr, walk, mapped_page, mem.TryTranslateWrite(phys_page));
            return mapped_page + offset;
        }
        ReportFault(client, MmuException::OutOfBound, gpu_addr);
        cache.gpu_page = 0xFFFFFFFFu;
        cache.read_page = nullptr;
        cache.write_page = nullptr;
        return TranslateSafeRead(gpu_addr);
    }

    if (Mmuv1LooksConfigured(client)) {
        uint32_t mmuv1_phys = 0u;
        if (!TranslateMmuv1(gpu_addr, client, mmuv1_phys)) return nullptr;
        return mem.TryTranslate(mmuv1_phys);
    }

    return mem.TryTranslate(gpu_addr);
}

uint8_t* VivanteMmu::TranslateToHostWrite(uint32_t gpu_addr, MmuClient client) const {
    auto& mem = emu_.Get<EmulatedMemory>();
    if (LooksConfigured()) {
        const uint32_t slot = static_cast<uint32_t>(client) & 3u;
        const uint32_t offset = gpu_addr & 0xFFFu;
        TranslationPageCache& cache = translation_cache_[slot];
        if (CacheMatches(cache, gpu_addr, client, true)) return cache.write_page + offset;

        WalkResult walk{};
        if (!Walk(gpu_addr, true, client, walk)) {
            cache.gpu_page = 0xFFFFFFFFu;
            cache.read_page = nullptr;
            cache.write_page = nullptr;
            return nullptr;
        }
        const uint32_t phys_page = walk.physical & ~0xFFFu;
        if (auto* mapped_page = mem.TryTranslateWrite(phys_page)) {
            FillCache(cache, gpu_addr, walk, mem.TryTranslate(phys_page), mapped_page);
            return mapped_page + offset;
        }
        ReportFault(client, MmuException::OutOfBound, gpu_addr);
        cache.gpu_page = 0xFFFFFFFFu;
        cache.read_page = nullptr;
        cache.write_page = nullptr;
        return nullptr;
    }

    if (Mmuv1LooksConfigured(client)) {
        uint32_t mmuv1_phys = 0u;
        if (!TranslateMmuv1(gpu_addr, client, mmuv1_phys)) return nullptr;
        return mem.TryTranslateWrite(mmuv1_phys);
    }

    return mem.TryTranslateWrite(gpu_addr);
}

bool VivanteMmu::TranslateToPhysical(uint32_t gpu_addr, bool write, MmuClient client, uint32_t& phys) const {
    WalkResult walk{};
    if (Walk(gpu_addr, write, client, walk)) {
        phys = walk.physical;
        return true;
    }
    if (!LooksConfigured()) return TranslateMmuv1(gpu_addr, client, phys);
    return false;
}

} // namespace imx6_vivante
