#pragma once

#include "../core/service.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

class CerfEmulator;

/* TraceContext — read-only snapshot the JIT hands to a trace handler.
   `regs[15] == pc`; `regs[13]` SP; `regs[14]` LR. ReadVa* peek the
   data-TLB fast path via ArmMmu::PeekDataTlb — nullopt when the page
   is not currently TLB-cached (no MMU walk, no abort raise). */
struct TraceContext {
    const uint32_t* regs;
    uint32_t        cpsr;
    uint32_t        pc;
    CerfEmulator&   emu;

    std::optional<uint8_t>  ReadVa8 (uint32_t va) const;
    std::optional<uint16_t> ReadVa16(uint32_t va) const;
    std::optional<uint32_t> ReadVa32(uint32_t va) const;
};

using TraceHandler   = std::function<void(const TraceContext&)>;
using TracePredicate = std::function<bool(const TraceContext&)>;

class TraceManager : public Service {
public:
    using Service::Service;
    void OnReady() override;

    /* Runs `register_fn` (which calls OnPc) iff `expected_crc32` matches the
       computed bundle CRC32; otherwise discards it. */
    void RegisterForBundle(uint32_t expected_crc32,
                           const std::function<void()>& register_fn);

    void OnPc(uint32_t runtime_va, TraceHandler handler);
    void OnPcFiltered(uint32_t       runtime_va,
                      TracePredicate predicate,
                      TraceHandler   handler);

    /* Hot-path predicate. Single map lookup; empty map = single
       branch on size(). */
    bool HasPcTrace (uint32_t pc) const;

    /* Dispatch site called from the JIT translator. */
    void DispatchPc(uint32_t pc, const uint32_t* regs, uint32_t cpsr);

    uint32_t BundleCrc32() const { return bundle_crc32_; }

#if CERF_DEV_MODE
    void OnRunLoopIter(TraceHandler handler);
    void DispatchRunLoopIter(const uint32_t* regs, uint32_t cpsr);
#endif

private:
    /* Computes CRC32 over concatenated RomParserService::Loaded()[i].raw
       bytes in load order. Empty when no ROMs loaded. */
    uint32_t ComputeBundleCrc32() const;

    uint32_t bundle_crc32_   = 0;
    uint32_t bundles_matched_ = 0;
    uint32_t bundles_skipped_ = 0;

    struct PcEntry {
        std::optional<TracePredicate> predicate;
        TraceHandler                  handler;
    };
    std::unordered_map<uint32_t, std::vector<PcEntry>> pc_traces_;

#if CERF_DEV_MODE
    std::vector<TraceHandler> iter_handlers_;
#endif
};
