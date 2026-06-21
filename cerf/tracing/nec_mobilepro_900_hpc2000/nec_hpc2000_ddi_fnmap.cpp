#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_cpu.h"
#include "../../jit/cpu_state.h"
#include "bundle.h"

#include <cstdint>

/* ddi.dll's DRVENABLEDATA (byte_11B14E8) is a flat PFN table, unmappable to DDI
   names statically. Hook every entry and cycle-stamp its fires so the call
   ORDER identifies DrvEnableSurface (gwes calls DrvEnablePDEV(0x11B8594) ->
   DrvCompletePDEV -> DrvEnableSurface). ddi.dll is XIP/gwes-only so unfiltered. */
namespace {

/* DrvEnablePDEV (0x011B8594) is omitted - it's already hooked elsewhere in this
   bundle's traces, and two unfiltered OnPc at one VA halt CERF. */
constexpr uint32_t kPfns[] = {
    0x011B7BF0u, 0x011B7C3Cu, 0x011B7C1Cu, 0x011B7C7Cu,
    0x011B7D14u, 0x011B82ACu, 0x011BC23Cu, 0x011B7ED4u, 0x011B86B4u,
    0x011B87D8u, 0x011B871Cu, 0x011B8724u, 0x011B8778u, 0x011B7E4Cu,
    0x011B7D70u, 0x011B7D54u, 0x011B8470u, 0x011BC074u, 0x011B1B84u,
    0x011BC18Cu, 0x011B7B88u, 0x011B7BA0u, 0x011B7BB8u,
};

class TraceNecHpc2000DdiFnMap : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Hpc2000BundleCrc32, [&] {
            for (uint32_t a : kPfns) {
                tm.OnPc(a, [a, n = uint32_t{0}](const TraceContext& c) mutable {
                    if (++n > 2) return;
                    LOG(Trace, "[ddi-fn] pc=0x%08X #%u cyc=0x%08X R0=0x%08X "
                               "R1=0x%08X\n",
                        a, n, c.emu.Get<ArmCpu>().State()->guest_cycle_counter,
                        c.regs[0], c.regs[1]);
                });
            }
        });
    }
};

REGISTER_SERVICE(TraceNecHpc2000DdiFnMap);

}  /* namespace */
