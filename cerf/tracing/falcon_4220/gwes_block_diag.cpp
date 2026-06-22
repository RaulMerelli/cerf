#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "bundle.h"

#if CERF_DEV_MODE

namespace {

/* Unfiltered across processes on purpose: the 28 s-idle park is an INFINITE
   wait whose root may be a service gwes waits on (not gwes itself), and the
   blocking slot is unknown until observed -- each fire logs slot+LR to
   attribute it. coredll shared XIP; WFSO timeout=R1, WFMO timeout=R3. */
class Falcon4220GwesBlockDiag : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            tm.OnPc(0x3F83558u, [](const TraceContext& c) {
                if (c.regs[1] != 0xFFFFFFFFu) return;   /* INFINITE only. */
                const uint32_t slot = c.emu.Get<ArmMmu>().State()->process_id >> 25;
                LOG(Trace, "[GWESBLK] WFSO INF slot=%u h=0x%08X LR=0x%08X\n",
                    slot, c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x3F834DCu, [](const TraceContext& c) {
                if (c.regs[3] != 0xFFFFFFFFu) return;   /* INFINITE only. */
                const uint32_t slot = c.emu.Get<ArmMmu>().State()->process_id >> 25;
                LOG(Trace, "[GWESBLK] WFMO INF slot=%u n=%u hArr=0x%08X LR=0x%08X\n",
                    slot, c.regs[0], c.regs[1], c.regs[14]);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Falcon4220GwesBlockDiag);

#endif  /* CERF_DEV_MODE */
