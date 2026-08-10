#pragma once

#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "../isa_block_space.h"
#include "../jit_code_arena.h"

struct MipsCpuState;

class EmulatedMemory;
class MipsMmu;

class MipsTranslationCache : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Mips;
    }

    JitCodeArena&  Arena()            { return arena_; }
    IsaBlockSpace& Space(bool m16)    { return m16 ? blocks16_ : blocks_; }

    void Flush();

    /* QEMU tlb_flush -> tcg_flush_jmp_cache: drop the VA jump cache on an
       address-space switch; the per-ASID block indices persist. */
    void ContextSwitchFlush();

    /* QEMU notdirty_write (accel/tcg/cputlb.c): a guest store onto a page that
       holds translated blocks drops the blocks its bytes overlap. `host` is the
       store's destination inside the DRAM region; `size` is 1, 2, 4 or 8. */
    void InvalidateOnRamWrite(uint8_t* host, uint32_t size);

    uint32_t BlockIndexKey(uint32_t phys_start);

    void SetInjectionBandHost(uint32_t pa, uint32_t size);
    void AddDmaRegion(uint32_t pa, uint32_t size);

private:
    struct DramHostRegion {
        uint8_t* host_lo    = nullptr;
        uint8_t* host_hi    = nullptr;
        uint32_t index_base = 0;
    };
    static constexpr int kMaxDramRegions = 4;

    struct DmaRegionEntry { uint8_t* base = nullptr; uint32_t size = 0; };
    static constexpr int kMaxDmaRegions = 4;

    uint32_t DramIndexOffset(const uint8_t* host) const {
        for (int i = 0; i < dram_region_count_; ++i) {
            const DramHostRegion& r = dram_regions_[i];
            if (host >= r.host_lo && host < r.host_hi) {
                return r.index_base + static_cast<uint32_t>(host - r.host_lo);
            }
        }
        return UINT32_MAX;
    }

    bool InInjectionBand(const uint8_t* host) const {
        return band_host_base_ && host >= band_host_base_ &&
               host < band_host_base_ + band_size_;
    }

    bool InDmaRegion(const uint8_t* host) const {
        for (int i = 0; i < dma_region_count_; ++i)
            if (host >= dma_regions_[i].base &&
                host < dma_regions_[i].base + dma_regions_[i].size)
                return true;
        return false;
    }

    JitCodeArena  arena_;
    IsaBlockSpace blocks_;
    IsaBlockSpace blocks16_;

    DramHostRegion dram_regions_[kMaxDramRegions];
    int            dram_region_count_ = 0;
    uint32_t       dram_index_size_   = 0;

    uint8_t* band_host_base_ = nullptr;
    uint32_t band_size_      = 0;

    DmaRegionEntry dma_regions_[kMaxDmaRegions];
    int            dma_region_count_ = 0;

    EmulatedMemory* memory_    = nullptr;
    MipsMmu*        mmu_       = nullptr;
    MipsCpuState*   cpu_state_ = nullptr;
};
