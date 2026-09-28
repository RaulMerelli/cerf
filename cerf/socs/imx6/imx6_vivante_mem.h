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
#include "../../peripherals/peripheral_base.h"
#include "imx6_gic.h"
#include "imx6_vivante_mmu.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_state_registers.h"

namespace imx6_vivante {

class VivanteMem {
public:
    VivanteMem(VivanteState& s, CerfEmulator& emu, VivanteCore core, int irq_spi)
        : s_(s), emu_(emu), core_(core), irq_spi_(irq_spi),
          mmu_(s, emu), state_registers_(s, *this) {}

    struct MaskedStateGroup {
        uint32_t field_mask;
        uint32_t preserve_mask;
    };

    VivanteCore Core() const { return core_; }
    bool Is2d() const { return core_ == VivanteCore::Gc3202d; }
    uint32_t PixelPipes() const {
        return core_ == VivanteCore::Gc3202d ? 2u : 1u;
    }

    bool SupportsTileStatus() const {
        return core_ == VivanteCore::Gc20003d;
    }

    bool SupportsFastClear() const { return SupportsTileStatus(); }
    bool SupportsColorCompression() const { return SupportsTileStatus(); }
    uint32_t TileStatusBitsPerTile() const { return SupportsTileStatus() ? 2u : 0u; }

    static constexpr uint32_t kTileStatusFunctionalMask = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4) | (1u << 5) |
                                                          (1u << 6) | (1u << 7) | (0xFu << 8) | (1u << 12) |
                                                          (1u << 13) | (1u << 14) | (1u << 30);

    uint32_t SanitizeTileStatusConfig(uint32_t value) const {
        if (!SupportsTileStatus()) return value & ~kTileStatusFunctionalMask;
        return value;
    }

    void FlushEngineCaches() const {
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    [[noreturn]] void HaltUnsupported(const char* op, uint32_t addr, uint64_t value) const {
        emu_.Get<Fatal>().Die("Vivante core %u rejected %s at 0x%08X (value 0x%016llX)", static_cast<uint32_t>(core_),
                              op, addr, static_cast<unsigned long long>(value));
    }

    bool DetectIdleRing(uint32_t pc, FeCommandAddressSpace address_space, IdleRingInfo& info) const;

    static bool IsGpuStateOffset(uint32_t off) {
        return VivanteStateRegisters::SupportsOffset(off);
    }

    void EnsureStateSize() {
        if (s_.state_.empty()) s_.state_.resize(kMaxStateBytes / 4u);
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
        if ((s_.semaphore_tokens_[to] & bit) == 0u) return false;
        s_.semaphore_tokens_[to] &= ~bit;
        return true;
    }

    template <size_t N>
    static uint32_t MergeMaskedState(uint32_t current, uint32_t value, const MaskedStateGroup (&groups)[N]) {
        for (const auto& group : groups) {
            if ((value & group.preserve_mask) == 0u) {
                current = (current & ~group.field_mask) | (value & group.field_mask);
            }
        }
        return current;
    }

    void StoreStateReg(uint32_t byte_off, uint32_t value);

    const uint8_t* TranslateCommandToHost(uint32_t address, size_t size,
                                          FeCommandAddressSpace address_space) const;
    bool ReadCommandBytes(uint32_t address, void* out_buffer, size_t count, FeCommandAddressSpace address_space) const;
    bool ReadCommandWords(uint32_t address, uint32_t* out, uint32_t count, FeCommandAddressSpace address_space) const;
    bool ReadMemoryWords(uint32_t address, uint32_t* out, uint32_t count) const;
    bool ReadGpuBytes(uint32_t address, void* out_buffer, size_t count, MmuClient client = MmuClient::Texture) const;
    bool WriteGpuBytes(uint32_t address, const void* in_buffer, size_t count,
                       MmuClient client = MmuClient::PixelEngine) const;
    bool ReadMemoryU64(uint32_t address, uint64_t& out) const;

    static uint32_t PatternBytesPerPixel(uint32_t fmt) {
        switch (fmt & 0xFu) {
        case 0u: return 2u;
        case 1u: return 2u;
        case 2u: return 2u;
        case 3u: return 2u;
        case 4u: return 2u;
        case 5u: return 4u;
        case 6u: return 4u;
        case 9u: return 1u;
        default: return 0u;
        }
    }

    const uint8_t* TranslateGpuToHost(uint32_t gpu_addr, size_t size,
                                      MmuClient client = MmuClient::Texture) const;
    uint8_t* TranslateGpuToHostWrite(uint32_t gpu_addr, size_t size,
                                     MmuClient client = MmuClient::PixelEngine) const;

    void RaiseInterrupt(uint32_t bits) const {
        s_.intr_status_ |= bits;
        UpdateIrq();
    }

    void UpdateIrq() const {
        const bool asserted = (s_.intr_status_ & s_.intr_enable_) != 0u;
        if (asserted == s_.irq_asserted_) return;
        s_.irq_asserted_ = asserted;
        auto& gic = emu_.Get<Imx6Gic>();
        if (asserted)
            gic.AssertSpi(irq_spi_);
        else
            gic.DeAssertSpi(irq_spi_);
    }

private:
    VivanteState& s_;
    CerfEmulator& emu_;
    VivanteCore core_;
    int irq_spi_;
    VivanteMmu mmu_;
    VivanteStateRegisters state_registers_;
};

}
