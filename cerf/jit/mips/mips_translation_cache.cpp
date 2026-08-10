#include "mips_translation_cache.h"

#include "../../boards/page_table_builder.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "mips_cpu.h"
#include "mips_cpu_state.h"
#include "mips_mmu.h"

REGISTER_SERVICE(MipsTranslationCache);

void MipsTranslationCache::OnReady() {
    memory_    = &emu_.Get<EmulatedMemory>();
    cpu_state_ = emu_.Get<MipsCpu>().State();

    arena_.Initialize();

    const auto dram = emu_.Get<PageTableBuilder>().CachedDramRegions();
    if (dram.empty() || static_cast<int>(dram.size()) > kMaxDramRegions) {
        LOG(Caution, "MipsTranslationCache: board declares %zu cached-DRAM "
                "regions; the SMC block index supports 1..%d\n",
            dram.size(), kMaxDramRegions);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t index_base = 0;
    for (const auto& r : dram) {
        uint8_t* host = memory_->TryTranslate(r.pa_base);
        if (!host || (r.size & 0xFFFu) != 0u) {
            LOG(Caution, "MipsTranslationCache: DRAM pa=0x%08X size=0x%X is not a "
                    "page-multiple backed region\n", r.pa_base, r.size);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        DramHostRegion& e = dram_regions_[dram_region_count_++];
        e.host_lo    = host;
        e.host_hi    = host + r.size;
        e.index_base = index_base;
        index_base  += r.size;
    }
    dram_index_size_ = index_base;
    blocks_.Initialize(0, dram_index_size_ >> 12);
    blocks16_.Initialize(0, dram_index_size_ >> 12);

    mmu_ = &emu_.Get<MipsMmu>();
    mmu_->Bind(&blocks_, &blocks16_);

    LOG(Jit, "MipsTranslationCache::OnReady: %d DRAM bank(s), %u SMC-indexed "
            "pages\n", dram_region_count_, dram_index_size_ >> 12);
}

void MipsTranslationCache::Flush() {
    arena_.Flush();
    blocks_.FlushAll();
    blocks16_.FlushAll();
}

void MipsTranslationCache::ContextSwitchFlush() {
    blocks_.JumpCacheFlush();
    blocks16_.JumpCacheFlush();
}

uint32_t MipsTranslationCache::BlockIndexKey(uint32_t phys_start) {
    uint8_t* host = memory_->TryTranslateWrite(phys_start);
    if (!host) return kBlockUnindexed;

    const uint32_t off = DramIndexOffset(host);
    if (off != UINT32_MAX) return off;
    if (InInjectionBand(host)) return kBlockUnindexed;
    LOG(Caution, "MipsTranslationCache::BlockIndexKey: block at pa=0x%08X "
            "pc=0x%08X is in a writable region that is neither DRAM nor the "
            "injection band; a store into it would not invalidate the block\n",
        phys_start, cpu_state_->pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void MipsTranslationCache::InvalidateOnRamWrite(uint8_t* host, uint32_t size) {
    const uint32_t off = DramIndexOffset(host);
    if (off == UINT32_MAX) {
        if (InInjectionBand(host)) return;
        if (InDmaRegion(host)) return;
        LOG(Caution, "MipsTranslationCache::InvalidateOnRamWrite: store of %u "
                "byte(s) at host %p pc=0x%08X lands in a writable region that is "
                "neither DRAM nor the injection band; self-modifying code there "
                "is unmodeled\n", size, host, cpu_state_->pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (blocks_.PageHasBlocks(off)) {
        blocks_.RemoveRange(off, off + size - 1u);
    }
    if (blocks16_.PageHasBlocks(off)) {
        blocks16_.RemoveRange(off, off + size - 1u);
    }
}

void MipsTranslationCache::SetInjectionBandHost(uint32_t pa, uint32_t size) {
    band_host_base_ = memory_->TryTranslateWrite(pa);
    band_size_      = band_host_base_ ? size : 0u;
}

void MipsTranslationCache::AddDmaRegion(uint32_t pa, uint32_t size) {
    uint8_t* base = memory_->TryTranslateWrite(pa);
    if (!base) return;
    if (dma_region_count_ >= kMaxDmaRegions) {
        LOG(Caution, "MipsTranslationCache::AddDmaRegion: more than %d DMA "
                "regions declared\n", kMaxDmaRegions);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    dma_regions_[dma_region_count_].base = base;
    dma_regions_[dma_region_count_].size = size;
    ++dma_region_count_;
}
