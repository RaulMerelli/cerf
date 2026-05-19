#pragma once

#include "../core/service.h"

#include <cstdint>
#include <mutex>
#include <vector>

class ArmMmu;
class Peripheral;

class PeripheralDispatcher : public Service {
public:
    using Service::Service;

    /* Cache the ArmMmu pointer that the JIT IO helpers read
       io_pending_address from. ArmMmu has no reverse dependency on
       PeripheralDispatcher, so the lazy service-locator resolution
       cannot cycle. */
    void OnReady() override;

    void Register(Peripheral* p);

    bool IsPeripheralAddress(uint32_t addr) const;

    uint8_t  ReadByte (uint32_t addr);
    uint16_t ReadHalf (uint32_t addr);
    uint32_t ReadWord (uint32_t addr);
    uint64_t ReadDword(uint32_t addr);
    void     WriteByte (uint32_t addr, uint8_t  value);
    void     WriteHalf (uint32_t addr, uint16_t value);
    void     WriteWord (uint32_t addr, uint32_t value);
    void     WriteDword(uint32_t addr, uint64_t value);

    static uint8_t  __fastcall JitIoReadByte (int8_t* hint, PeripheralDispatcher* d);
    static uint16_t __fastcall JitIoReadHalf (int8_t* hint, PeripheralDispatcher* d);
    static uint32_t __fastcall JitIoReadWord (int8_t* hint, PeripheralDispatcher* d);
    static void     __fastcall JitIoWriteByte(int8_t* hint, PeripheralDispatcher* d, uint8_t  value);
    static void     __fastcall JitIoWriteHalf(int8_t* hint, PeripheralDispatcher* d, uint16_t value);
    static void     __fastcall JitIoWriteWord(int8_t* hint, PeripheralDispatcher* d, uint32_t value);

    uint32_t* LastGuestPcPtr() { return &last_guest_pc_; }

    /* Direct call entry that skips mutex + virtual dispatch + Peripheral
       base. Sa1110OsTimer registers its lambdas here in OnReady. */
    using FastReadFn  = uint32_t (*)(void* ctx, uint32_t off, uint32_t width_bytes);
    using FastWriteFn = void     (*)(void* ctx, uint32_t off, uint32_t value, uint32_t width_bytes);

    void RegisterFastPath(uint32_t base, uint32_t end,
                          FastReadFn r, FastWriteFn w, void* ctx) {
        fast_base_  = base;
        fast_end_   = end;
        fast_read_  = r;
        fast_write_ = w;
        fast_ctx_   = ctx;
    }

private:
    struct Entry {
        uint32_t    base;
        uint32_t    size;     /* end = base + size, exclusive */
        Peripheral* p;
    };

    /* Sorted by base ascending. Touched on Register, read on every
       address routing call. */
    std::vector<Entry> entries_;
    mutable std::mutex io_lock_;

    /* Wired in OnReady — cached so the JIT IO helpers can reach
       io_pending_address_ without a service-locator lookup on the
       hot path. */
    ArmMmu* mmu_ = nullptr;

    uint32_t last_guest_pc_ = 0;

    uint32_t    fast_base_  = 0;
    uint32_t    fast_end_   = 0;
    FastReadFn  fast_read_  = nullptr;
    FastWriteFn fast_write_ = nullptr;
    void*       fast_ctx_   = nullptr;

    /* Returns the peripheral covering addr, or nullptr if no
       peripheral claims it. Caller must hold io_lock_. */
    Peripheral* Lookup(uint32_t addr) const;

    Entry* LookupEntry(uint32_t addr) const;
};
