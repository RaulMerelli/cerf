#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "bundle.h"

#include <cstdint>

/* hpc2000 parks spinning on the OST (nk.exe sub_840BD5E8 + leaf helpers at
   0x840BE3xx) with intc_assert=0 though INTC bit26(OST) is unmasked. Poll
   OST+INTC to see whether OIER.E0 is armed and OSSR.M0 / OSCR ever advance. */
namespace {

class TraceNecHpc2000OstProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Hpc2000BundleCrc32, [&] {
            tm.OnRunLoopIter([prev_oier = uint32_t{0xFFFFFFFFu},
                              prev_ossr = uint32_t{0xFFFFFFFFu},
                              prev_icpr = uint32_t{0xFFFFFFFFu},
                              logs = 0](const TraceContext& c) mutable {
                auto& pd = c.emu.Get<PeripheralDispatcher>();
                const uint32_t oier  = pd.ReadWord(0x40A0001Cu);
                const uint32_t ossr  = pd.ReadWord(0x40A00014u);
                const uint32_t oscr  = pd.ReadWord(0x40A00010u);
                const uint32_t osmr0 = pd.ReadWord(0x40A00000u);
                const uint32_t icmr  = pd.ReadWord(0x40D00004u);
                const uint32_t icpr  = pd.ReadWord(0x40D00010u);
                if (oier == prev_oier && ossr == prev_ossr && icpr == prev_icpr)
                    return;
                if (++logs <= 80)
                    LOG(Trace, "[HPC-OST] OIER=0x%X OSSR=0x%X OSMR0=0x%08X "
                               "OSCR=0x%08X (OSCR-OSMR0=%d) ICMR=0x%08X ICPR=0x%08X "
                               "ost26{masked=%d pend=%d}\n",
                        oier, ossr, osmr0, oscr, (int)(oscr - osmr0), icmr, icpr,
                        ((icmr >> 26) & 1u) == 0, (icpr >> 26) & 1u);
                prev_oier = oier;
                prev_ossr = ossr;
                prev_icpr = icpr;
            });

            /* OST tick handler (updates the 64-bit tick count, re-arms OSMR0).
               If this only fires a handful of times it is not being driven by
               the timer interrupt. */
            tm.OnPc(0x840BD5E8u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n <= 10 || (n % 500) == 0)
                    LOG(Trace, "[HPC-TICK] sub_840BD5E8 #%u LR=0x%08X\n",
                        n, c.regs[14]);
            });
        });
    }
};

REGISTER_SERVICE(TraceNecHpc2000OstProbe);

}  /* namespace */
