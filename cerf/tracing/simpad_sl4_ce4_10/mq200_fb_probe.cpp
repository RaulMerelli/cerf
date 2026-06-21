#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/mediaq_mq200/mediaq_mq200.h"
#include "bundle.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#if CERF_DEV_MODE

namespace {

/* Off-thread sampler: logs an MQ200 visible-window content hash on every change.
   Time-based (not OnRunLoopIter) so it catches screens that render after the
   guest idles, when Run() returns - and thus OnRunLoopIter - nearly stop. */
class SimpadSl4Mq200FbProbe : public Service {
public:
    using Service::Service;

    void OnShutdown() override {
        stop_.store(true, std::memory_order_release);
        if (thread_.joinable()) thread_.join();
    }

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            thread_ = std::thread([this] { SampleLoop(); });
        });
    }

private:
    std::atomic<bool> stop_{false};
    std::thread       thread_;

    void SampleLoop() {
        auto& mq = emu_.Get<MediaQMq200>();
        uint32_t shots = 0;
        uint64_t last  = 0;
        while (!stop_.load(std::memory_order_acquire) && shots < 30u) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            if (mq.Bpp() != 16u) continue;
            const uint32_t gw = mq.GetGuestW(), gh = mq.GetGuestH();
            const uint32_t stride = mq.Stride(), off = mq.FbWindowOffset();
            if (gw == 0u || gh == 0u || stride == 0u) continue;
            const uint8_t* base = mq.FbBytes();
            if (static_cast<uint64_t>(off) + static_cast<uint64_t>(stride) * gh
                    > mq.FbSize()) continue;
            uint64_t h = 1469598103934665603ull;   /* FNV-1a over sampled pixels */
            bool any = false;
            for (uint32_t y = 0; y < gh; y += 8u) {
                const uint8_t* line = base + off + static_cast<uint64_t>(y) * stride;
                for (uint32_t x = 0; x < gw * 2u; x += 16u) {
                    const uint8_t b = line[x];
                    if (b) any = true;
                    h = (h ^ b) * 1099511628211ull;
                }
            }
            if (!any) continue;
            if (h == last) continue;
            last = h;
            LOG(Trace, "[MQ200FB] frame %02u content-changed %ux%u bpp=%u stride=%u hash=0x%llX\n",
                shots, gw, gh, mq.Bpp(), stride, (unsigned long long)h);
            ++shots;
        }
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4Mq200FbProbe);

#endif  // CERF_DEV_MODE
