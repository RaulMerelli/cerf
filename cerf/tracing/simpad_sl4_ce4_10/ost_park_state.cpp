#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "bundle.h"

#include <memory>

#if CERF_DEV_MODE

namespace {

/* device.exe's Sleep(100) never wakes - the OST match stops firing in the idle
   park. Sample the SA-1110 OST registers each window to tell apart: no match
   armed (oier clear / ossr stuck set) vs match armed but OSCR not crossing
   OSMR0 (frozen guest cycles). */
class SimpadSl4OstParkState : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            auto n = std::make_shared<uint64_t>(0);
            tm.OnRunLoopIter([n](const TraceContext& c) {
                if ((*n)++ % 30000ull) return;
                auto& pd = c.emu.Get<PeripheralDispatcher>();
                const uint32_t osmr0 = pd.ReadWord(0x90000000u);
                const uint32_t oscr  = pd.ReadWord(0x90000010u);
                const uint32_t ossr  = pd.ReadWord(0x90000014u);
                const uint32_t oier  = pd.ReadWord(0x9000001Cu);
                const uint32_t icip  = pd.ReadWord(0x90050000u);
                const uint32_t icmr  = pd.ReadWord(0x90050004u);
                const uint32_t icpr  = pd.ReadWord(0x90050020u);
                LOG(Trace, "[OSTPARK] osmr0=0x%08X oscr=0x%08X ossr=0x%X oier=0x%X "
                           "armed=%d | icip=0x%08X icmr=0x%08X icpr=0x%08X "
                           "ost26: pend=%d masked=%d ip=%d\n",
                    osmr0, oscr, ossr, oier, (oier & ~ossr & 0x1u) != 0,
                    icip, icmr, icpr,
                    (icpr >> 26) & 1, ((icmr >> 26) & 1) == 0, (icip >> 26) & 1);
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4OstParkState);

#endif  // CERF_DEV_MODE
