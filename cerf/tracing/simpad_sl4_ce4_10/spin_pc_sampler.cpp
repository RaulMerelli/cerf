#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#if CERF_DEV_MODE

namespace {

/* The post-FS-mount stall is a pure-compute loop (no MMIO, so the PERF mmio_pc
   histogram can't see it). Histogram the guest PC at each Run() return and dump
   the hottest PCs per window; the window is cleared each dump so the late spin
   isn't drowned by the early high-throughput boot phase. */
class SimpadSl4SpinPcSampler : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            auto hist = std::make_shared<std::unordered_map<uint32_t, uint64_t>>();
            auto n    = std::make_shared<uint64_t>(0);
            tm.OnRunLoopIter([hist, n](const TraceContext& c) {
                (*hist)[c.pc]++;
                if (++*n % 30000ull) return;
                std::vector<std::pair<uint32_t, uint64_t>> v(hist->begin(), hist->end());
                const size_t k = std::min<size_t>(6, v.size());
                std::partial_sort(v.begin(), v.begin() + k, v.end(),
                                  [](const auto& a, const auto& b) { return a.second > b.second; });
                for (size_t i = 0; i < k; ++i)
                    LOG(Trace, "[SPINPC] window@%llu pc=0x%08X count=%llu\n",
                        (unsigned long long)*n, v[i].first,
                        (unsigned long long)v[i].second);
                hist->clear();
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4SpinPcSampler);

#endif  // CERF_DEV_MODE
