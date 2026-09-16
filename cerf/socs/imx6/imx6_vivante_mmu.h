#pragma once

#include "imx6_vivante_state.h"

#include <cstdint>

class CerfEmulator;

namespace imx6_vivante {

class VivanteMmu {
public:
    VivanteMmu(VivanteState& state, CerfEmulator& emu, int irq_spi)
        : state_(state), emu_(emu), irq_spi_(irq_spi) {}

    void InvalidateTranslationCache() const;
    void WriteConfiguration(uint32_t value);
    void WriteSafeAddress(uint32_t value);
    void Reset();
    const uint8_t* TranslateToHost(
        uint32_t gpu_address,
        MmuClient client = MmuClient::Texture) const;
    uint8_t* TranslateToHostWrite(
        uint32_t gpu_address,
        MmuClient client = MmuClient::PixelEngine) const;
    bool TranslateToPhysical(uint32_t gpu_address, bool write,
                             MmuClient client, uint32_t& physical) const;

private:
    struct TranslationPageCache {
        uint32_t gpu_page = 0xFFFFFFFFu;
        uint32_t configuration = 0u;
        uint32_t mtlb_base = 0u;
        uint32_t mtlb_entry_address = 0u;
        uint32_t mtlb_descriptor = 0u;
        uint32_t pte_entry_address = 0u;
        uint32_t pte = 0u;
        const uint8_t* read_page = nullptr;
        uint8_t* write_page = nullptr;
    };

    struct WalkResult {
        uint32_t physical = 0u;
        uint32_t page_shift = 12u;
        uint32_t configuration = 0u;
        uint32_t mtlb_base = 0u;
        uint32_t mtlb_entry_address = 0u;
        uint32_t mtlb_descriptor = 0u;
        uint32_t pte_entry_address = 0u;
        uint32_t pte = 0u;
    };

    bool LooksConfigured() const;
    bool Mmuv1LooksConfigured(MmuClient client) const;
    static uint32_t Mmuv1PageTableRegister(MmuClient client);
    static uint32_t Mmuv1MemoryBaseRegister(MmuClient client);
    bool TranslateMmuv1(uint32_t gpu_address, MmuClient client,
                        uint32_t& physical) const;
    uint32_t ActiveMtlbBase(MmuClient client) const;
    static uint32_t MtlbShift(uint32_t configuration);
    static uint32_t PageShift(uint32_t page_type);
    bool ReadPhysicalU32(uint32_t address, uint32_t& value) const;
    bool Walk(uint32_t gpu_address, bool write, MmuClient client,
              WalkResult& result) const;
    bool CacheMatches(const TranslationPageCache& cache,
                      uint32_t gpu_address, MmuClient client,
                      bool write) const;
    void FillCache(TranslationPageCache& cache, uint32_t gpu_address,
                   const WalkResult& result, const uint8_t* read_page,
                   uint8_t* write_page) const;
    void ReportFault(MmuClient client, MmuException exception,
                     uint32_t gpu_address) const;
    const uint8_t* TranslateSafeRead(uint32_t fault_address) const;
    void UpdateIrq() const;

    VivanteState& state_;
    CerfEmulator& emu_;
    int irq_spi_;
    mutable TranslationPageCache translation_cache_[4]{};
};

} // namespace imx6_vivante
