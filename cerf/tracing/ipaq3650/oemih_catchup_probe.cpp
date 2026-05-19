#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "bundle.h"

#include <atomic>
#include <intrin.h>

#if CERF_DEV_MODE

namespace {

class IpaqOemihCatchupProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kIpaq3650BundleCrc32, [&] {
            static std::atomic<uint64_t> oemih_entries{0};
            static std::atomic<uint64_t> oemih_exits{0};
            static std::atomic<uint64_t> oemih_tsc_total{0};
            static std::atomic<uint64_t> catchup_entries{0};
            static std::atomic<uint64_t> catchup_iters{0};
            static std::atomic<int64_t>  gap_sum{0};
            static std::atomic<uint64_t> gap_samples{0};
            static thread_local uint64_t entry_tsc{0};

            tm.OnPc(0x800AA1BCu, [](const TraceContext& /*c*/) {
                oemih_entries.fetch_add(1, std::memory_order_relaxed);
                entry_tsc = __rdtsc();
            });

            tm.OnPc(0x800AABB4u, [](const TraceContext& /*c*/) {
                const uint64_t now = __rdtsc();
                if (entry_tsc != 0) {
                    oemih_tsc_total.fetch_add(
                        now - entry_tsc, std::memory_order_relaxed);
                    entry_tsc = 0;
                }
                oemih_exits.fetch_add(1, std::memory_order_relaxed);
            });

            static std::atomic<uint32_t> gap_ring_pos{0};
            static std::atomic<int32_t>  gap_ring[64]{};
            static std::atomic<uint32_t> gap_osmr_ring[64]{};
            static std::atomic<uint32_t> gap_oscr_ring[64]{};

            tm.OnPc(0x800AA3CCu, [](const TraceContext& c) {
                catchup_entries.fetch_add(1, std::memory_order_relaxed);
                auto& pd = c.emu.Get<PeripheralDispatcher>();
                const uint32_t osmr0 = pd.ReadWord(0x90000000u);
                const uint32_t oscr  = pd.ReadWord(0x90000010u);
                const int32_t gap = static_cast<int32_t>(osmr0 - oscr);
                gap_sum.fetch_add(gap, std::memory_order_relaxed);
                gap_samples.fetch_add(1, std::memory_order_relaxed);
                const uint32_t slot =
                    gap_ring_pos.fetch_add(1, std::memory_order_relaxed) & 63u;
                gap_ring[slot].store(gap, std::memory_order_relaxed);
                gap_osmr_ring[slot].store(osmr0, std::memory_order_relaxed);
                gap_oscr_ring[slot].store(oscr,  std::memory_order_relaxed);
            });

            tm.OnPc(0x800AA3E0u, [](const TraceContext& /*c*/) {
                catchup_iters.fetch_add(1, std::memory_order_relaxed);
            });

            tm.OnRunLoopIter([](const TraceContext& /*c*/) {
                static uint64_t last_log_tsc = 0;
                const uint64_t now = __rdtsc();
                /* TSC freq approx 4.5 GHz on host, 4.3 GHz on VM; either
                   way 4.4e9 ticks ≈ 1 second. Cheap threshold to fire the
                   summary once per second without coupling to wallclock. */
                if (last_log_tsc != 0 && (now - last_log_tsc) < 4'400'000'000ull) {
                    return;
                }
                last_log_tsc = now;

                const uint64_t oe   = oemih_entries.exchange(0, std::memory_order_relaxed);
                const uint64_t ox   = oemih_exits.exchange(0, std::memory_order_relaxed);
                const uint64_t otsc = oemih_tsc_total.exchange(0, std::memory_order_relaxed);
                const uint64_t ce   = catchup_entries.exchange(0, std::memory_order_relaxed);
                const uint64_t ci   = catchup_iters.exchange(0, std::memory_order_relaxed);
                const int64_t  gs   = gap_sum.exchange(0, std::memory_order_relaxed);
                const uint64_t gn   = gap_samples.exchange(0, std::memory_order_relaxed);

                const uint64_t iters_per_catchup = ce ? (ci / ce) : 0;
                const uint64_t oemih_us = otsc / 4400ull;
                const int64_t  mean_gap = gn ? (gs / static_cast<int64_t>(gn)) : 0;

                LOG(Trace,
                    "[OEMIH] calls=%llu exits=%llu oemih_us=%llu "
                    "catchup_entries=%llu catchup_iters=%llu "
                    "iters_per_catchup=%llu mean_signed_gap=%lld\n",
                    (unsigned long long)oe, (unsigned long long)ox,
                    (unsigned long long)oemih_us,
                    (unsigned long long)ce, (unsigned long long)ci,
                    (unsigned long long)iters_per_catchup,
                    (long long)mean_gap);

                const uint32_t collected =
                    gap_ring_pos.exchange(0, std::memory_order_relaxed);
                const uint32_t to_show = collected < 64u ? collected : 64u;
                for (uint32_t i = 0; i < to_show; ++i) {
                    const int32_t  g  = gap_ring[i].load(std::memory_order_relaxed);
                    const uint32_t mo = gap_osmr_ring[i].load(std::memory_order_relaxed);
                    const uint32_t co = gap_oscr_ring[i].load(std::memory_order_relaxed);
                    LOG(Trace,
                        "[OEMIH-ENTRY] #%u OSMR0=0x%08X OSCR=0x%08X gap=%d\n",
                        i, mo, co, g);
                }
            });
        });
    }
};

REGISTER_SERVICE(IpaqOemihCatchupProbe);

}  /* namespace */

#endif  /* CERF_DEV_MODE */
