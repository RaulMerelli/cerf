#pragma once

#include <cstddef>
#include <cstdint>

/* SCTLR / Control Register bit layout. */
union ArmCp15ControlRegister {
    struct {
        uint32_t m         : 1;   /* MMU enable */
        uint32_t a         : 1;   /* alignment check enable */
        uint32_t c         : 1;   /* data cache enable */
        uint32_t w         : 1;   /* write buffer enable */
        uint32_t p         : 1;   /* exception-handler ISA: 0=26-bit, 1=32-bit */
        uint32_t d         : 1;   /* 26/32-bit address checking */
        uint32_t l         : 1;   /* abort model (obsolete) */
        uint32_t b         : 1;   /* big-endian */
        uint32_t s         : 1;   /* system protect */
        uint32_t r         : 1;   /* ROM protect */
        uint32_t reserved2 : 1;
        uint32_t z         : 1;   /* branch target buffer enable */
        uint32_t i         : 1;   /* instruction cache enable */
        uint32_t v         : 1;   /* exception vector relocation (high vectors) */
        uint32_t rr        : 1;   /* cache replacement strategy */
        uint32_t l4        : 1;   /* load instruction bit-0 ignore-Thumb-mode */
        uint32_t reserved3 : 16;
    } bits;
    uint32_t word;
};
static_assert(sizeof(ArmCp15ControlRegister) == 4, "control register must be 32 bits");

/* ACTLR / Auxiliary Control Register (PXA/ARM-implementation-specific). */
union ArmCp15AuxControlRegister {
    struct {
        uint32_t k         : 1;   /* write coalescing */
        uint32_t p         : 1;   /* page table memory */
        uint32_t reserved1 : 2;
        uint32_t md        : 2;   /* mini data cache */
        uint32_t reserved2 : 26;
    } bits;
    uint32_t word;
};
static_assert(sizeof(ArmCp15AuxControlRegister) == 4, "aux control register must be 32 bits");

/* TTBR / Translation Table Base Register — 16 KB-aligned base of the
   L1 page table; low 14 bits are reserved. */
union ArmCp15TranslationTableBase {
    struct {
        uint32_t reserved1 : 14;
        uint32_t base      : 18;
    } bits;
    uint32_t word;
};
static_assert(sizeof(ArmCp15TranslationTableBase) == 4, "TTBR must be 32 bits");

/* FSR / Fault Status Register. */
union ArmCp15FaultStatus {
    struct {
        uint32_t status   : 4;
        uint32_t domain   : 4;
        uint32_t reserved : 1;
        uint32_t d        : 1;   /* debug event */
        uint32_t x        : 1;   /* status field extension (FS[4] in v7) */
        uint32_t wnr      : 1;   /* v7 write-not-read (ARM DDI 0406B B3.13.4) */
        uint32_t sbz      : 20;
    } bits;
    uint32_t word;
};
static_assert(sizeof(ArmCp15FaultStatus) == 4, "FSR must be 32 bits");

/* FSR.Status codes. */
namespace ArmFaultStatus {
    constexpr uint32_t kVectorException            = 0;
    constexpr uint32_t kAlignment                  = 1;
    constexpr uint32_t kTerminalException          = 2;
    constexpr uint32_t kAlignmentAlt               = 3;
    constexpr uint32_t kExternalAbortLineFetchSection = 4;
    constexpr uint32_t kTranslationSection         = 5;
    constexpr uint32_t kExternalAbortLineFetchPage = 6;
    constexpr uint32_t kTranslationPage            = 7;
    constexpr uint32_t kExternalAbortSection       = 8;
    constexpr uint32_t kDomainSection              = 9;
    constexpr uint32_t kExternalAbortPage          = 10;
    constexpr uint32_t kDomainPage                 = 11;
    constexpr uint32_t kExternalAbortTranslation1  = 12;
    constexpr uint32_t kPermissionSection          = 13;
    constexpr uint32_t kExternalAbortTranslation2  = 14;
    constexpr uint32_t kPermissionPage             = 15;
}

/* Access kind passed to the TLB lookup. The JIT picks ITLB vs DTLB
   based on whether the access is a fetch or a data load/store; read
   vs write distinction further selects permission bits per the AP
   encoding. */
enum class ArmMmuAccess : uint8_t {
    kNone        = 0,
    kRead        = 2,
    kWrite       = 4,
    kReadWrite   = 6,
    kExecute     = 10,
};

/* One TLB slot. PTE caches the L1/L2 descriptor word; HostAdjust is
   (host_pointer - guest_VA) for the resolved page so the JIT-emit
   fast path can compute the host load address with a single add. */
struct ArmTlbSlot {
    uint32_t  virtual_address;
    uint32_t  pte;
    uintptr_t host_adjust;
    bool      valid;
};

/* TLB unit — one per access kind (ITLB / DTLB). The TLB is a small
   fixed-size cache; eviction is round-robin via next_free_slot. */
constexpr size_t kArmTlbSlotCount = 64;

struct ArmTlbUnit {
    int8_t      next_free_slot;
    ArmTlbSlot  slots[kArmTlbSlotCount];
};

/* Aggregate cp15 + TLB state. The MMU service holds exactly one
   instance; emitted JIT code reads/writes fields by absolute byte
   offset into this struct. */
struct ArmMmuState {
    ArmCp15ControlRegister       control_register;
    ArmCp15AuxControlRegister    aux_control_register;
    ArmCp15TranslationTableBase  translation_table_base;
    uint32_t                     domain_access_control;
    ArmCp15FaultStatus           fault_status;
    uint32_t                     fault_address;
    uint32_t                     process_id;
    uint32_t                     coprocessor_access;

    uint32_t                     cssel_register;

    uint32_t                     ttbr1;        /* c2  CRm=0 op2=1 */
    uint32_t                     ttbcr;        /* c2  CRm=0 op2=2 */
    uint32_t                     prrr;         /* c10 CRm=2 op2=0 */
    uint32_t                     nmrr;         /* c10 CRm=2 op2=1 */
    uint32_t                     contextidr;   /* c13 CRm=0 op2=1 */
    uint32_t                     tpidrurw;     /* c13 CRm=0 op2=2 */
    uint32_t                     tpidruro;     /* c13 CRm=0 op2=3 */
    uint32_t                     tpidrprw;     /* c13 CRm=0 op2=4 */

    /* TLB scan-bias slot shared by all LDREX/STREX call sites. The
       bias only affects which TLB way is checked first, never
       correctness — LDREX/STREX are kernel-lock instructions,
       infrequent enough that one shared slot suffices. */
    int8_t                       ldrex_strex_tlb_hint;

    ArmTlbUnit                   data_tlb;
    ArmTlbUnit                   instruction_tlb;
};

/* FCSE address fold. When the MMU is enabled and the guest VA is in
   the bottom 32 MB, the high 7 bits are taken from the process ID
   (CONTEXTIDR / FCSEIDR) — so multiple processes each see a private
   0..32 MB slot while the underlying page tables are global. */
inline uint32_t ApplyFcseFold(const ArmMmuState& state, uint32_t va) {
    if (state.control_register.bits.m && (va & 0xFE000000u) == 0u) {
        return va | state.process_id;
    }
    return va;
}
