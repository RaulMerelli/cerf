#pragma once

#include <cstdint>
#include <vector>

namespace imx6_vivante {

enum class VivanteCore : uint8_t {
    Gc20003d,
    Gc3202d,
    Gc355Vg,
};

enum FeOpcode : uint32_t {
    kFeLoadState = 1,
    kFeEnd = 2,
    kFeNop = 3,
    kFeDraw2d = 4,
    kFeDrawPrimitives = 5,
    kFeDrawIndexedPrimitives = 6,
    kFeWait = 7,
    kFeLink = 8,
    kFeStall = 9,
    kFeCall = 10,
    kFeReturn = 11,
    kFeDrawInstanced = 12,
    kFeChipSelect = 13,
    kFeWaitFence = 15,
    kFeDrawIndirect = 16,
    kFeSnapPages = 19,
};

inline constexpr uint32_t kFeCommandEnable = 0x00010000u;
inline constexpr uint32_t kFeCommandPrefetchMask = 0x0000FFFFu;
inline constexpr uint32_t kFeCallStackDepth = 16u;
inline constexpr uint32_t kMaxStateBytes = 0x20000u;

inline constexpr uint32_t FePrefetchDwords(uint32_t prefetch64) {
    return prefetch64 * 2u;
}

inline constexpr uint32_t FeAlignedCommandWords(uint32_t semantic_words) {
    return (semantic_words + 1u) & ~1u;
}

inline constexpr uint32_t FeDraw2dPacketWords(uint32_t rectangle_count, uint32_t data_count) {
    return 2u + rectangle_count * 2u + FeAlignedCommandWords(data_count);
}
/* Linux etnaviv_iommu.c defines one shared MMUv1 raw-PA page table; etnaviv
   state_hi.xml defines the MC page-table registers. */
inline constexpr uint32_t kMmuv1GpuMemStart = 0x80000000u;
inline constexpr uint32_t kMmuv1PageMask = 0xFFFFF000u;
inline constexpr uint32_t kMmuv1FePageTable = 0x400u;
inline constexpr uint32_t kMmuv1TxPageTable = 0x404u;
inline constexpr uint32_t kMmuv1PePageTable = 0x408u;
inline constexpr uint32_t kMmuv1RaPageTable = 0x410u;
inline constexpr uint32_t kMmuv1MemoryBaseRa = 0x418u;
inline constexpr uint32_t kMmuv1MemoryBaseFe = 0x41Cu;
inline constexpr uint32_t kMmuv1MemoryBaseTx = 0x420u;
inline constexpr uint32_t kMmuv1MemoryBasePe = 0x428u;

enum class MmuClient : uint8_t {
    Fe = 0,
    Texture = 1,
    PixelEngine = 2,
    Rasterizer = 3,
};

struct FeStats {
    bool idle_ring = false;
    bool stopped = false;
    bool blocked = false;
};

enum class FeCommandAddressSpace : uint8_t {
    Physical,
    Virtual,
};

struct FeCallFrame {
    uint32_t return_address = 0;
    uint32_t return_window_words = 0;
    FeCommandAddressSpace return_address_space = FeCommandAddressSpace::Virtual;
};

struct IdleRingInfo {
    uint32_t base = 0;
    uint32_t target = 0;
    FeCommandAddressSpace address_space = FeCommandAddressSpace::Physical;
};

struct VivanteState {
    uint32_t regs_[0x4000u / 4u]{};
    uint32_t intr_status_ = 0;
    uint32_t intr_enable_ = 0;
    bool irq_asserted_ = false;
    bool fe_live_ = false;
    bool fe_idle_ring_ = false;
    bool fe_in_advance_ = false;
    uint32_t fe_ring_pc_ = 0;
    uint32_t fe_ring_prefetch_ = 0;
    uint32_t fe_window_words_ = 0;
    uint32_t fe_resume_idle_target_ = 0;
    FeCommandAddressSpace fe_address_space_ = FeCommandAddressSpace::Physical;
    FeCommandAddressSpace fe_resume_address_space_ = FeCommandAddressSpace::Physical;
    FeCallFrame fe_call_stack_[kFeCallStackDepth]{};
    uint32_t fe_call_depth_ = 0;
    uint32_t semaphore_tokens_[32]{};
    uint8_t de_pattern_latch_[256]{};
    uint32_t de_pattern_latch_config_ = 0;
    uint32_t de_pattern_latch_address_ = 0;
    uint32_t de_pattern_latch_bpp_ = 0;
    bool de_pattern_latch_valid_ = false;
    std::vector<uint32_t> state_;
};

}
