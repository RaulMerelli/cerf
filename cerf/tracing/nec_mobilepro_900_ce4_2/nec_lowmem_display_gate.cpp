#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

#if CERF_DEV_MODE

/* Polls the ddi.dll VRAM-allocator gate LowMemory+0x1A854 across boot to find
   any guest writer (logs initial value + every change). */
namespace {

constexpr uint32_t kLowMemUncachedVa = 0xB0004000u;
constexpr uint32_t kGateVa = kLowMemUncachedVa + 0x1A854u; /* the *v14 gate */

class TraceNecLowmemDisplayGate : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&] {
            tm.OnRunLoopIter([last = uint32_t{0xFFFFFFFFu}, dumped = false,
                              logs = 0](const TraceContext& c) mutable {
                auto gate = c.ReadVa32(kGateVa);
                if (!gate) return; /* page not yet data-TLB resident */

                if (!dumped) {
                    dumped = true;
                    LOG(Trace, "[LOWMEM-GATE] region live; +0x1A84C=%08X "
                               "+0x1A850=%08X +0x1A854=%08X +0x1A860=%08X "
                               "+0x1A870=%08X +0x1A7E0=%08X\n",
                        c.ReadVa32(kLowMemUncachedVa + 0x1A84Cu).value_or(0xDEADBEEFu),
                        c.ReadVa32(kLowMemUncachedVa + 0x1A850u).value_or(0xDEADBEEFu),
                        *gate,
                        c.ReadVa32(kLowMemUncachedVa + 0x1A860u).value_or(0xDEADBEEFu),
                        c.ReadVa32(kLowMemUncachedVa + 0x1A870u).value_or(0xDEADBEEFu),
                        c.ReadVa32(kLowMemUncachedVa + 0x1A7E0u).value_or(0xDEADBEEFu));
                }

                if (*gate != last) {
                    if (++logs <= 16)
                        LOG(Trace, "[LOWMEM-GATE] +0x1A854 changed 0x%08X -> 0x%08X\n",
                            last, *gate);
                    last = *gate;
                }
            });
        });
    }
};

REGISTER_SERVICE(TraceNecLowmemDisplayGate);

}  /* namespace */

#endif  /* CERF_DEV_MODE */
