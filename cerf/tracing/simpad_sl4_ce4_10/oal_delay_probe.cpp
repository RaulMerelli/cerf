#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <atomic>
#include <memory>

#if CERF_DEV_MODE

namespace {

/* sub_800B30B8 is the OAL OSCR busy-delay (target = now + scaled(R0), spin until
   OSCR reaches it). Boot stalls spinning here; log R0 (delay arg) and caller to
   tell a single huge delay from a delay storm. */
class SimpadSl4OalDelayProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            auto n = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0x800B30B8u, [n](const TraceContext& c) {
                uint32_t i = n->fetch_add(1);
                if (i < 40 || (i % 1000) == 0)
                    LOG(Trace, "[OALDELAY] #%u arg=%d (0x%08X) lr=0x%08X\n",
                        i, int32_t(c.regs[0]), c.regs[0], c.regs[14]);
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4OalDelayProbe);

#endif  // CERF_DEV_MODE
