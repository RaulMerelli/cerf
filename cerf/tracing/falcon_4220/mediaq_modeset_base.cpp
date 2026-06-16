#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "bundle.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

/* ddi.dll is XIP: hook the raw link VA (loadVA+offset never fires). ddi.dll
   maps only in gwes, so these user-VA OnPc hooks are unambiguous (unfiltered). */
class FalconMediaqModesetBase : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            /* sub_18415A0(a1=FB base, a2=stride, a3=bpp): GC0CR := a1. */
            tm.OnPc(0x18415A0u, [](const TraceContext& c) {
                static std::atomic<int> n{0};
                if (n.fetch_add(1, std::memory_order_relaxed) >= 24) return;
                LOG(SocReset,
                    "[MODESET] sub_18415A0 base(R0)=0x%08X stride(R1)=0x%08X "
                    "bpp(R2)=0x%08X caller(LR)=0x%08X cpsr=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[14], c.cpsr);
            });
            /* sub_1841990(enable): copies live GC0CR -> GE0BR. Log GC0CR + the
               current GE0BR so the propagation of the bad base is visible. */
            tm.OnPc(0x1841990u, [](const TraceContext& c) {
                static std::atomic<int> n{0};
                if (n.fetch_add(1, std::memory_order_relaxed) >= 24) return;
                auto& d = c.emu.Get<PeripheralDispatcher>();
                /* GE0BR via the queued alias (0x142C) not the direct block
                   (0x22C = SD/MMC region); same GE register, no SD-path route. */
                LOG(SocReset,
                    "[MODESET] sub_1841990 enable(R0)=0x%08X GC0CR=0x%08X "
                    "GE0BR(pre)=0x%08X caller(LR)=0x%08X\n",
                    c.regs[0], d.ReadWord(0x080401B0u), d.ReadWord(0x0804142Cu),
                    c.regs[14]);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconMediaqModesetBase);

#endif  /* CERF_DEV_MODE */
