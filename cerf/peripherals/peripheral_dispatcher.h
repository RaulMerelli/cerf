#pragma once

#include "../core/service.h"
#include "peripheral_base.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

enum class MmioWidth : uint32_t { kByte = 1u, kHalf = 2u, kWord = 4u };
enum class ResetLineKind;

enum class ResetBaselinePolicy {
    EveryReset,
    ColdResetOnly,
};

class PeripheralDispatcher : public Service {
public:
    using Service::Service;

    void OnAllReady() override;

    void Register(Peripheral* p);
    /* Register an MMIO peripheral and capture its initialized register/FIFO
       state as the hardware reset baseline. The i.MX6 reset domain uses this
       only for blocks reset by SRC; retention domains use Register(). */
    void RegisterResettable(Peripheral* p,
                            ResetBaselinePolicy policy = ResetBaselinePolicy::EveryReset);

    bool IsPeripheralAddress(uint32_t addr) const;

    void ValidatePhysReachable(uint32_t phys_addr_mask) const;

    std::vector<Peripheral*> RegisteredPeripherals() const;

    uint8_t  ReadByte (uint32_t addr);
    uint16_t ReadHalf (uint32_t addr);
    uint32_t ReadWord (uint32_t addr);
    uint64_t ReadDword(uint32_t addr);
    void     WriteByte (uint32_t addr, uint8_t  value);
    void     WriteHalf (uint32_t addr, uint16_t value);
    void     WriteWord (uint32_t addr, uint32_t value);
    void     WriteDword(uint32_t addr, uint64_t value);

private:
    struct ResetBaseline {
        Peripheral* p;
        ResetBaselinePolicy policy;
        std::vector<uint8_t> state;
    };

    void RestoreResetBaselines(ResetLineKind reset_kind);
    struct Entry {
        uint32_t                base;
        uint32_t                end;      /* exclusive */
        Peripheral::FastReadFn  read;
        Peripheral::FastWriteFn write;
        void*                   ctx;
        Peripheral*             p;
    };

    using EntryTable = std::vector<Entry>;

public:
    uint32_t Read(uint32_t addr, MmioWidth width) {
        if (const Entry* e = MemoHit(addr)) {
            return ClipToWidth(
                e->read(e->ctx, addr - e->base, static_cast<uint32_t>(width)),
                width);
        }
        return ReadSlow(addr, width);
    }

    void Write(uint32_t addr, uint32_t value, MmioWidth width) {
        if (const Entry* e = MemoHit(addr)) {
            e->write(e->ctx, addr - e->base, ClipToWidth(value, width),
                     static_cast<uint32_t>(width));
            return;
        }
        WriteSlow(addr, value, width);
    }

private:
    /* ARM DDI 0406C.c A8.8.68 LDRB (immediate, ARM) and A8.8.80 LDRH
       (immediate, ARM): R[t] = ZeroExtend(MemU[address,N], 32). */
    static uint32_t ClipToWidth(uint32_t value, MmioWidth width) {
        switch (width) {
        case MmioWidth::kByte: return value & 0xFFu;
        case MmioWidth::kHalf: return value & 0xFFFFu;
        case MmioWidth::kWord: return value;
        }
        HaltBadWidth(static_cast<uint32_t>(width));
    }

    const Entry* MemoHit(uint32_t addr) const {
        const EntryTable* t = live_.load(std::memory_order_acquire);
        if (!t) return nullptr;
        const size_t cached = last_hit_.load(std::memory_order_relaxed);
        if (cached >= t->size()) return nullptr;
        const Entry& hit = (*t)[cached];
        if (addr < hit.base || addr >= hit.end) return nullptr;
        return &hit;
    }

    [[noreturn]] static void HaltBadWidth(uint32_t width);

    uint32_t ReadSlow(uint32_t addr, MmioWidth width);
    void     WriteSlow(uint32_t addr, uint32_t value, MmioWidth width);
    const Entry* LookupSlow(uint32_t addr) const;

    std::atomic<const EntryTable*>          live_{nullptr};
    std::vector<std::unique_ptr<EntryTable>> tables_;

    mutable std::atomic<size_t> last_hit_{0};

    std::vector<ResetBaseline> reset_baselines_;
    bool reset_baseline_listener_registered_ = false;

    const Entry* LookupEntry(uint32_t addr) const;
};
