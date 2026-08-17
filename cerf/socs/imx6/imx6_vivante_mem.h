#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../jit/arm/arm_mmu_probe.h"
#include "../../peripherals/peripheral_base.h"
#include "imx6_gic.h"
#include "imx6_vivante_state.h"

namespace imx6_vivante {

/* Vivante GC memory/MMU access + register-state helpers + FE ring detection +
   interrupt line. Leaf of the engine-helper DAG: operates on VivanteState by
   reference and reaches guest memory / the GIC through emu_. */
class VivanteMem {
public:
    VivanteMem(VivanteState& s, CerfEmulator& emu, Peripheral& owner,
               VivanteCore core, int irq_spi)
        : s_(s), emu_(emu), owner_(owner), core_(core), irq_spi_(irq_spi) {}

    struct TranslationPageCache {
        uint32_t gpu_page = 0xFFFFFFFFu;
        uint32_t configuration = 0u;
        uint32_t mtlb_base = 0u;
        uint32_t mtlb_entry_addr = 0u;
        uint32_t mtlb_desc = 0u;
        uint32_t pte_entry_addr = 0u;
        uint32_t pte = 0u;
        const uint8_t* read_page = nullptr;
        uint8_t* write_page = nullptr;
    };

    struct MmuWalkResult {
        uint32_t phys = 0u;
        uint32_t page_shift = 12u;
        uint32_t configuration = 0u;
        uint32_t mtlb_base = 0u;
        uint32_t mtlb_entry_addr = 0u;
        uint32_t mtlb_desc = 0u;
        uint32_t pte_entry_addr = 0u;
        uint32_t pte = 0u;
    };

    struct MaskedStateGroup {
        uint32_t field_mask;
        uint32_t preserve_mask;
    };

    void InvalidateTranslationCache() const {
        for (uint32_t i = 0u; i < 4u; ++i) {
            translation_cache_[i] = TranslationPageCache{};
        }
    }

    const char* CoreName() const {
        switch (core_) {
        case VivanteCore::Gc3202d: return "2D";
        case VivanteCore::Gc355Vg: return "VG";
        default: return "3D";
        }
    }
    VivanteCore Core() const { return core_; }
    bool Is2d() const { return core_ == VivanteCore::Gc3202d; }
    uint32_t PixelPipes() const {
        /* etna_viv tools/data/gpus.json for i.MX6 DualLite: GC320 has two
           pixel pipes, GC880 has one.  The VG block does not submit RS work;
           keep a single logical pipe so malformed state cannot index past the
           two-pipe register arrays. */
        return core_ == VivanteCore::Gc3202d ? 2u : 1u;
    }

    bool SupportsTileStatus() const {
        /* The advertised GC880 identity has FAST_CLEAR/Z_COMPRESSION and
           2BITPERTILE.  The GC320 identity has neither fast clear nor depth
           compression; accepting TS state there makes a normal 2D surface
           look compressed and produces stale/black rectangles. */
        return core_ == VivanteCore::Gc8803d;
    }

    bool SupportsFastClear() const { return SupportsTileStatus(); }
    bool SupportsColorCompression() const { return SupportsTileStatus(); }
    uint32_t TileStatusBitsPerTile() const {
        return SupportsTileStatus() ? 2u : 0u;
    }

    static constexpr uint32_t kTileStatusFunctionalMask =
        (1u << 0)  | (1u << 1)  | (1u << 3)  | (1u << 4) |
        (1u << 5)  | (1u << 6)  | (1u << 7)  | (0xFu << 8) |
        (1u << 12) | (1u << 13) | (1u << 14) | (1u << 30);

    uint32_t SanitizeTileStatusConfig(uint32_t value) const {
        if (!SupportsTileStatus())
            return value & ~kTileStatusFunctionalMask;
        return value;
    }

    void FlushEngineCaches(uint32_t mask) const {
        /* CERF exposes guest RAM immediately to every synchronous engine.
           Thus render/TS cache flushes are ordering pulses, not persistent
           state and not MMUv2 TLB invalidations.  A host fence preserves the
           ordering contract without discarding the live page-walk cache. */
        (void)mask;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    [[noreturn]] void HaltUnsupported(const char* op, uint32_t addr,
                                      uint64_t value) const {
        emu_.Get<Fatal>().Die(
            "Vivante core %u rejected %s at 0x%08X (value 0x%016llX)",
            static_cast<uint32_t>(core_), op, addr,
            static_cast<unsigned long long>(value));
    }

    bool DetectIdleRing(uint32_t pc, FeCommandAddressSpace address_space,
                        IdleRingInfo& info) const;

    static bool IsGpuStateOffset(uint32_t off) {
        return off < kMaxStateBytes && (off & 3u) == 0u;
    }

    void EnsureStateSize() {
        if (s_.state_.empty())
            s_.state_.resize(kMaxStateBytes / 4u);
    }

    uint32_t StateReg(uint32_t byte_off) const {
        const uint32_t idx = byte_off >> 2;
        return idx < s_.state_.size() ? s_.state_[idx] : 0u;
    }

    void ArmSemaphoreToken(uint32_t token) {
        const uint32_t from = token & 0x1Fu;
        const uint32_t to = (token >> 8) & 0x1Fu;
        s_.semaphore_tokens_[to] |= 1u << from;
    }

    bool TryConsumeSemaphoreToken(uint32_t token) {
        const uint32_t from = token & 0x1Fu;
        const uint32_t to = (token >> 8) & 0x1Fu;
        const uint32_t bit = 1u << from;
        if ((s_.semaphore_tokens_[to] & bit) == 0u)
            return false;
        s_.semaphore_tokens_[to] &= ~bit;
        return true;
    }

    template <size_t N>
    static uint32_t MergeMaskedState(
        uint32_t current, uint32_t value,
        const MaskedStateGroup (&groups)[N]) {
        /* Vivante masked state is active-low: when the preserve bit is one,
           the associated field retains its previous value.  Mask bits are
           command metadata and are not stored as ordinary register state. */
        for (const auto& group : groups) {
            if ((value & group.preserve_mask) == 0u) {
                current = (current & ~group.field_mask) |
                          (value & group.field_mask);
            }
        }
        return current;
    }

    void StoreStateReg(uint32_t byte_off, uint32_t value);
    void WriteMmuv2Configuration(uint32_t value);
    void WriteMmuv2SafeAddress(uint32_t value);
    void ResetMmuv2State();

    const uint8_t* TranslateCommandToHost(
        uint32_t address, FeCommandAddressSpace address_space) const;
    bool ReadCommandBytes(uint32_t address, void* out_buffer, size_t count,
                          FeCommandAddressSpace address_space) const;
    bool ReadCommandWords(uint32_t address, uint32_t* out, uint32_t count,
                          FeCommandAddressSpace address_space) const;
    bool ReadMemoryWords(uint32_t address, uint32_t* out,
                         uint32_t count) const;
    bool ReadGpuBytes(uint32_t address, void* out_buffer, size_t count,
                      MmuClient client = MmuClient::Texture) const;
    bool WriteGpuBytes(uint32_t address, const void* in_buffer, size_t count,
                       MmuClient client = MmuClient::PixelEngine) const;
    bool ReadMemoryU64(uint32_t address, uint64_t& out) const;
    void DumpCommandWords(uint32_t address, uint32_t prefetch,
                          FeCommandAddressSpace address_space) const;

    bool MmuLooksConfigured() const {
        /* MMUv2 translation starts only after CONTROL.ENABLE.  The page-table
           address can be programmed while the setup command buffer is still
           fetched physically, so CONFIGURATION.ADDRESS is not an enable bit. */
        return (s_.regs_[kMmuv2Control >> 2] & 1u) != 0u;
    }

    static uint32_t Mmuv1PageTableRegister(MmuClient client) {
        static constexpr uint32_t registers[] = {
            kMmuv1FePageTable,
            kMmuv1TxPageTable,
            kMmuv1PePageTable,
            kMmuv1RaPageTable,
        };
        return registers[static_cast<uint32_t>(client)];
    }

    static uint32_t Mmuv1MemoryBaseRegister(MmuClient client) {
        static constexpr uint32_t registers[] = {
            kMmuv1MemoryBaseFe,
            kMmuv1MemoryBaseTx,
            kMmuv1MemoryBasePe,
            kMmuv1MemoryBaseRa,
        };
        return registers[static_cast<uint32_t>(client)];
    }

    bool Mmuv1LooksConfigured(MmuClient client) const {
        return s_.regs_[Mmuv1PageTableRegister(client) >> 2] != 0u;
    }

    bool TranslateMmuv1(uint32_t gpu_addr, MmuClient client,
                        uint32_t& phys) const;

    uint32_t ActiveMtlbBase(MmuClient client) const;

    static uint32_t Mmuv2MtlbShift(uint32_t configuration) {
        return (configuration & kMmuv2Mode1k) != 0u
            ? kMmuv2Mtlb1kShift : kMmuv2Mtlb4kShift;
    }

    static uint32_t Mmuv2PageShift(uint32_t page_type) {
        static constexpr uint32_t shifts[4] = {12u, 16u, 20u, 24u};
        return shifts[page_type & 3u];
    }

    static uint32_t PatternBytesPerPixel(uint32_t fmt) {
        switch (fmt & 0xFu) {
        case 0u: return 2u; /* X4R4G4B4 */
        case 1u: return 2u; /* A4R4G4B4 */
        case 2u: return 2u; /* X1R5G5B5 */
        case 3u: return 2u; /* A1R5G5B5 */
        case 4u: return 2u; /* R5G6B5 */
        case 5u: return 4u; /* X8R8G8B8 */
        case 6u: return 4u; /* A8R8G8B8 */
        case 9u: return 1u; /* INDEX8 */
        default: return 0u;
        }
    }

    bool ReadPhysicalU32(uint32_t address, uint32_t& value) const {
        const uint8_t* entry = emu_.Get<EmulatedMemory>().TryTranslate(address);
        if (!entry)
            return false;
        std::memcpy(&value, entry, sizeof(value));
        return true;
    }

    bool WalkMmuv2(uint32_t gpu_addr, bool write, MmuClient client,
                   MmuWalkResult& walk) const;
    bool TranslationCacheMatches(const TranslationPageCache& cache,
                                 uint32_t gpu_addr, MmuClient client,
                                 bool write) const;
    void FillTranslationCache(TranslationPageCache& cache,
                              uint32_t gpu_addr, const MmuWalkResult& walk,
                              const uint8_t* read_page,
                              uint8_t* write_page) const;
    void ReportMmuFault(MmuClient client, MmuException exception,
                        uint32_t gpu_addr) const;
    const uint8_t* TranslateMmuSafeRead(uint32_t fault_addr) const;
    const uint8_t* TranslateGpuToHost(
        uint32_t gpu_addr, MmuClient client = MmuClient::Texture) const;
    uint8_t* TranslateGpuToHostWrite(
        uint32_t gpu_addr,
        MmuClient client = MmuClient::PixelEngine) const;
    bool TranslateGpuViaMmu(uint32_t gpu_addr, bool write,
                            MmuClient client, uint32_t& phys) const;

    void RaiseInterrupt(uint32_t bits) const {
        s_.intr_status_ |= bits;
        UpdateIrq();
    }

    void UpdateIrq() const {
        const bool asserted = (s_.intr_status_ & s_.intr_enable_) != 0u;
        if (asserted == s_.irq_asserted_)
            return;
        s_.irq_asserted_ = asserted;
        if (auto* gic = emu_.TryGet<Imx6Gic>()) {
            if (asserted)
                gic->AssertSpi(irq_spi_);
            else
                gic->DeAssertSpi(irq_spi_);
        }
    }

private:
    VivanteState& s_;
    CerfEmulator& emu_;
    Peripheral& owner_;
    VivanteCore core_;
    int irq_spi_;
    mutable TranslationPageCache translation_cache_[4]{};
};

}  // namespace imx6_vivante
