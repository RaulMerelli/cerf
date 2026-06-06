#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {

/* Samples the JIT thread's guest PC after each Run() return and dumps the
   hottest PCs periodically. During the post-taskbar hang the dominant PC
   names the spin/idle site — kernel idle (all threads blocked, waiting on
   an IRQ that never fires) vs a driver busy-wait. */
class HangPcSampler : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            emu_.Get<TraceManager>().OnRunLoopIter([this](const TraceContext& c) {
                hist_[c.pc]++;
                if (++count_ >= kDumpEvery) { Dump(); count_ = 0; hist_.clear(); }
            });
        });
    }

private:
    static constexpr uint32_t kDumpEvery = 2000000u;

    void Dump() {
        std::vector<std::pair<uint32_t, uint32_t>> v(hist_.begin(), hist_.end());
        std::partial_sort(v.begin(),
                          v.begin() + std::min<size_t>(8, v.size()), v.end(),
                          [](auto& a, auto& b) { return a.second > b.second; });
        const size_t n = std::min<size_t>(8, v.size());
        for (size_t i = 0; i < n; ++i)
            LOG(Trace, "[HANGPC] top%zu pc=0x%08X count=%u\n",
                i + 1, v[i].first, v[i].second);
    }

    std::unordered_map<uint32_t, uint32_t> hist_;
    uint32_t count_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(HangPcSampler);

#endif  /* CERF_DEV_MODE */
