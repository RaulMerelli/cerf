#pragma once

#include <cstdint>
#include <vector>

namespace imx6_vivante {

/* Physical Vivante blocks present on i.MX6 Solo/DualLite.  Keep the core
   identity separate from the renderer capability: GC355 is OpenVG, not a
   second GC320, and GC880 must never execute DRAW_2D. */
enum class VivanteCore : uint8_t {
    Gc8803d,
    Gc3202d,
    Gc355Vg,
};

/* Vivante FE command opcodes (word >> 27). etnaviv rnndb cmdstream.xml. */
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

inline constexpr uint32_t FeDraw2dPacketWords(uint32_t rectangle_count,
                                              uint32_t data_count) {
    return 2u + rectangle_count * 2u +
           FeAlignedCommandWords(data_count);
}
/* Vivante MMUv2 page-table format.  MODE4_K has VA[31:22] MTLB
   indices and supports 4 KiB / 64 KiB leaves.  MODE1_K has VA[31:24]
   MTLB indices and additionally supports 1 MiB / 16 MiB leaves.  MTLB
   bits 3:2 select leaf size; the STLB pointer is 64-byte aligned. */
inline constexpr uint32_t kMmuv2SafeAddress = 0x180u;
inline constexpr uint32_t kMmuv2Configuration = 0x184u;
inline constexpr uint32_t kMmuv2Status = 0x188u;
inline constexpr uint32_t kMmuv2Control = 0x18Cu;
inline constexpr uint32_t kMmuv2ExceptionAddress = 0x190u;
inline constexpr uint32_t kMmuv2Interrupt = 1u << 30;
inline constexpr uint32_t kMmuv2Mode1k = 1u << 0;
inline constexpr uint32_t kMmuv2Mtlb4kMask = 0xFFC00000u;
inline constexpr uint32_t kMmuv2Mtlb4kShift = 22u;
inline constexpr uint32_t kMmuv2Mtlb1kMask = 0xFF000000u;
inline constexpr uint32_t kMmuv2Mtlb1kShift = 24u;
/* Backward-compatible names used by the existing MODE4_K tests/helpers. */
inline constexpr uint32_t kMmuv2MtlbMask = kMmuv2Mtlb4kMask;
inline constexpr uint32_t kMmuv2MtlbShift = kMmuv2Mtlb4kShift;
inline constexpr uint32_t kMmuv2StlbMask = 0x003FF000u;
inline constexpr uint32_t kMmuv2StlbShift = 12u;
inline constexpr uint32_t kMmuv2PtePresent = 1u << 0;
inline constexpr uint32_t kMmuv2PteException = 1u << 1;
inline constexpr uint32_t kMmuv2PteWriteable = 1u << 2;
inline constexpr uint32_t kMmuv2MtlbPageSizeMask = 3u << 2;
inline constexpr uint32_t kMmuv2MtlbPage4k = 0u << 2;
inline constexpr uint32_t kMmuv2MtlbPage64k = 1u << 2;
inline constexpr uint32_t kMmuv2MtlbPage1m = 2u << 2;
inline constexpr uint32_t kMmuv2MtlbPage16m = 3u << 2;
inline constexpr uint32_t kMmuv2StlbAddressMask = 0xFFFFFFC0u;
inline constexpr uint32_t kMmuv2PageMask = 0xFFFFF000u;

/* Vivante MMUv1 uses one flat array of raw 4 KiB physical page addresses
   for the upper half of GPU space.  Each client also has a linear-window
   memory base for addresses below GPU_MEM_START.  References: Linux
   drivers/gpu/drm/etnaviv/etnaviv_iommu.c (etnaviv_iommuv1_map/restore),
   etna_viv rnndb/state_hi.xml MC stripe, and KTP400 libGALCore.dll IDA
   sub_EF0058BC/sub_EF007218/sub_EF00AC34. */
inline constexpr uint32_t kMmuv1GpuMemStart = 0x80000000u;
inline constexpr uint32_t kMmuv1PageMask = 0xFFFFF000u;
inline constexpr uint32_t kMmuv1FePageTable = 0x400u;
inline constexpr uint32_t kMmuv1TxPageTable = 0x404u;
inline constexpr uint32_t kMmuv1PePageTable = 0x408u;
inline constexpr uint32_t kMmuv1PezPageTable = 0x40Cu;
inline constexpr uint32_t kMmuv1RaPageTable = 0x410u;
inline constexpr uint32_t kMmuv1MemoryBaseRa = 0x418u;
inline constexpr uint32_t kMmuv1MemoryBaseFe = 0x41Cu;
inline constexpr uint32_t kMmuv1MemoryBaseTx = 0x420u;
inline constexpr uint32_t kMmuv1MemoryBasePez = 0x424u;
inline constexpr uint32_t kMmuv1MemoryBasePe = 0x428u;

enum class MmuClient : uint8_t {
    Fe = 0,
    Texture = 1,
    PixelEngine = 2,
    Rasterizer = 3,
};

enum class MmuException : uint32_t {
    None = 0,
    SlaveNotPresent = 1,
    PageNotPresent = 2,
    WriteViolation = 3,
    OutOfBound = 4,
    ReadSecurityViolation = 5,
    WriteSecurityViolation = 6,
};

struct FeStats {
    uint32_t commands = 0;
    uint32_t load_state = 0;
    uint32_t draw_2d = 0;
    uint32_t draw_3d = 0;
    uint32_t links = 0;
    uint32_t waits = 0;
    uint32_t events = 0;
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

/* Vivante GC register file + front-end state, shared by the Peripheral MMIO
   layer and the VivanteMem / VivanteBlit / VivanteFe engine helpers. The owning
   Imx6Gpu3d Peripheral holds one instance and the recursive_mutex that
   serialises access to it; helpers operate on it by reference under that lock. */
struct VivanteState {
    uint32_t regs_[0x4000u / 4u]{};
    uint32_t intr_status_ = 0;
    uint32_t intr_enable_ = 0;
    bool irq_asserted_ = false;
    /* MMUv2 SAFE_ADDRESS is write-once after reset. */
    bool mmu_safe_address_written_ = false;
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
    /* semaphore_tokens_[TO] has one bit per FROM recipient.  A token is armed
       by GL.SEMAPHORE_TOKEN and consumed by GL.STALL_TOKEN or FE STALL. */
    uint32_t semaphore_tokens_[32]{};
    uint32_t chip_select_mask_ = 0;
    uint8_t de_pattern_latch_[256]{};
    uint32_t de_pattern_latch_config_ = 0;
    uint32_t de_pattern_latch_address_ = 0;
    uint32_t de_pattern_latch_bpp_ = 0;
    bool de_pattern_latch_valid_ = false;
    std::vector<uint32_t> state_;
};

}  // namespace imx6_vivante
