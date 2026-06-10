#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

namespace {

/* sub_800B44F0 runs flash detect only if base 0x8C09A23C == -1 (its nk.exe
   .data init); it reads 0, so detect never runs. Dump the .data globals to tell
   "section not relocated" (all 0) from "relocated wrong" (A008 == 0x800B20D8
   but A23C != -1). */
class SimpadSl4FlashBaseInit : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            tm.OnPc(0x800B44F0u, [](const TraceContext& c) {
                auto rd = [&](uint32_t va) {
                    auto v = c.ReadVa32(va);
                    return v ? *v : 0xDEADBEEFu;
                };
                LOG(Trace, "[FLBASE] sub_800B44F0 entry: "
                           "[A000]=0x%08X [A008]=0x%08X [A23C]=0x%08X "
                           "[A240]=0x%08X lr=0x%08X\n",
                    rd(0x8C09A000u), rd(0x8C09A008u), rd(0x8C09A23Cu),
                    rd(0x8C09A240u), c.regs[14]);
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4FlashBaseInit);

#endif  // CERF_DEV_MODE
