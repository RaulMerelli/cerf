#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <chrono>
#include <cstdint>

/* Passive touch-chain observation: pco accept/pen-up, calib stable-point, normal
   delivery callback. Each hooked DLL is XIP in one process, so unfiltered OnPc is
   unambiguous. */
namespace {

static long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

class TraceNecTouchChainProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&tm] {
            /* IDA-verified: 0x1BC2BE8 writes word_1BC4080=100 (accept/position),
               0x1BC2960 writes word_1BC4080=0 (pen-up). */
            tm.OnPc(0x1BC2BE8u, [](const TraceContext&) {
                LOG(Trace, "[PCO-ACCEPT] t=%lldms latch=100\n", NowMs());
            });
            tm.OnPc(0x1BC2960u, [](const TraceContext&) {
                LOG(Trace, "[PCO-PENUP] t=%lldms latch=0\n", NowMs());
            });
            tm.OnPc(0x1BA2148u, [](const TraceContext& c) {
                LOG(Trace, "[TOUCH-RATE] t=%lldms TouchPanelSetMode rate=%u (poll=%ums)\n",
                    NowMs(), c.regs[0], c.regs[0] ? 5000u / c.regs[0] : 0u);
            });
            tm.OnPc(0x1BA3B58u, [](const TraceContext&) {
                LOG(Trace, "[CALIB-POINT] t=%lldms stable point captured\n", NowMs());
            });
            tm.OnPc(0x1BA3C60u, [](const TraceContext& c) {
                const uint32_t f = c.regs[0];
                LOG(Trace, "[TOUCH-CB] t=%lldms flags=0x%X down=%u prevdown=%u\n",
                    NowMs(), f, (f >> 1) & 1u, (f >> 3) & 1u);
            });
        });
    }
};

REGISTER_SERVICE(TraceNecTouchChainProbe);

}  /* namespace */
