#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* P177 NDIS wrapper probe (dev). ndis.dll is at a fixed shared-DLL VA with no
   slot aliasing and only network drivers call these entries, so the unfiltered
   OnPc is exact. Logs DriverObject (r1) + caller LR (r14) at NdisInitializeWrapper
   to name the miniport passing the bogus 0x504D444E ("NDMP") handle. */

namespace {

class TraceSiemensP177NdisWrapper : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSiemensTp177bBundleCrc32, [this, &tm] {
            tm.OnPc(0x3BE1524u, [this](const TraceContext& c) {   /* NdisInitializeWrapper */
                if (init_++ >= 8u) return;
                LOG(Trace, "[P177NDIS] NdisInitializeWrapper DriverObject=0x%08X "
                           "regPathPtr=0x%08X LR=0x%08X\n",
                    c.regs[1], c.regs[2], c.regs[14]);
            });
            tm.OnPc(0x3BE15A8u, [this](const TraceContext& c) {   /* NdisTerminateWrapper */
                if (term_++ >= 8u) return;
                LOG(Trace, "[P177NDIS] NdisTerminateWrapper handle=0x%08X "
                           "handle[0]=0x%08lX LR=0x%08X\n",
                    c.regs[0],
                    c.ReadVa32(c.regs[0]) ? (unsigned long)*c.ReadVa32(c.regs[0]) : 0xDEADul,
                    c.regs[14]);
            });
        });
    }

private:
    uint32_t init_ = 0;
    uint32_t term_ = 0;
};

REGISTER_SERVICE(TraceSiemensP177NdisWrapper);

}  /* namespace */
