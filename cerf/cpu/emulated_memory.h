#pragma once

#include "../core/service.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>

class StateWriter;
class StateReader;

class EmulatedMemory : public Service {
public:
    using Service::Service;

    void OnReady() override;

    static constexpr uint32_t PAGE_SIZE = 0x1000;

    static constexpr size_t kMaxRegions = 64;

    void AddRegion(uint32_t base, uint32_t size,
                   DWORD page_protect = PAGE_READWRITE);

    uint8_t* Translate(uint32_t vaddr);

    uint8_t* TryTranslate(uint32_t paddr);

    /* TryTranslateWrite returns nullptr for PAGE_READONLY /
       PAGE_EXECUTE_READ regions so writes dispatch as MMIO. */
    uint8_t* TryTranslateWrite(uint32_t paddr);

    /* True iff the slot-granularity range containing `pa` lies entirely within
       one backed region (a single host_adjust is valid for the whole TLB slot).
       Section/large slots straddling a region boundary or peripheral hole
       return false so the MMU routes them per access. pte[1:0] = granularity. */
    bool IsSlotRangeUniform(uint32_t pte, uint32_t pa);

    uint8_t  ReadByte(uint32_t vaddr);
    uint16_t ReadHalf(uint32_t vaddr);
    uint32_t ReadWord(uint32_t vaddr);
    uint64_t ReadDword(uint32_t vaddr);
    void     WriteByte(uint32_t vaddr, uint8_t  value);
    void     WriteHalf(uint32_t vaddr, uint16_t value);
    void     WriteWord(uint32_t vaddr, uint32_t value);
    void     WriteDword(uint32_t vaddr, uint64_t value);

    /* Bulk copy from host buffer into emulated memory at vaddr. The
       entire range must fall inside one declared region; halts on
       unmapped destination or boundary-crossing copy. */
    void CopyIn(uint32_t vaddr, const void* host_src, size_t size);

    /* Bulk copy out of emulated memory. Same one-region contract as
       CopyIn. */
    void CopyOut(uint32_t vaddr, void* host_dst, size_t size);

    /* Power-cycle RAM loss: zero every backed volatile region. Flash
       (PAGE_READONLY / PAGE_EXECUTE_READ) keeps its contents — guest NOR
       writes survive a real power cycle. JIT thread at reset delivery
       only; anywhere else the memset races guest stores. */
    void WipeVolatileRegions();

    /* State image: snapshot / restore every volatile region (the set
       WipeVolatileRegions touches). Flash regions go through the separate
       SaveFlashRegions/RestoreFlashRegions pair below. */
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

    /* State image: snapshot / restore the backed flash regions (PAGE_READONLY /
       PAGE_EXECUTE_READ). A cold boot re-populates these from the ROM image, so
       without this capture a restore reverts guest NOR/NAND writes to ROM. */
    void SaveFlashRegions(StateWriter& w);
    void RestoreFlashRegions(StateReader& r);

    /* Total bytes across volatile regions — the save-progress denominator. */
    uint64_t VolatileByteCount() const;

private:
    struct Region {
        uint32_t              base         = 0;
        uint32_t              size         = 0;
        DWORD                 page_protect = 0;
        std::atomic<uint8_t*> host_ptr{nullptr};  /* lazy first-touch */
    };

    /* Lock-free read: load_acquire count_, linear scan regions_[0..n). */
    Region*  FindRegion(uint32_t vaddr);
    /* Shared CopyIn/CopyOut gate: region containing [vaddr, vaddr+size)
       or fatal (unmapped / boundary-crossing). */
    Region*  BulkRegionFor(uint32_t vaddr, size_t size, const char* op);
    /* Atomic first-touch CAS on host_ptr. Halts on VirtualAlloc fail. */
    uint8_t* EnsureBacked(Region* r);

    Region              regions_[kMaxRegions]{};
    std::atomic<size_t> count_{0};
    std::mutex          add_mutex_;
};
