#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"
#include "bundle.h"

#include <atomic>
#include <memory>

#if CERF_DEV_MODE

namespace {

/* The post-display-enable boot stalls in a tight busy-spin at user VA 0x01311AC0
   (spin_pc_sampler). Capture pid + regs there to learn which process spins and
   what pointer/flag the loop polls (the stall = GWES never drawing the desktop). */
class SimpadSl4GwesSpinProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            /* Unfiltered ON PURPOSE: identifying which process owns this slot VA
               is the goal - the handler logs process_id as the discovery, so a
               pid filter would hide the answer. */
            auto n = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0x01311AC0u, [n](const TraceContext& c) {
                if (n->fetch_add(1) >= 16u) return;
                LOG(Trace, "[GWESSPIN] pid=0x%08X lr=0x%08X "
                           "r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X "
                           "r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X\n",
                    c.emu.Get<ArmMmu>().State()->process_id, c.regs[14],
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[4], c.regs[5], c.regs[6], c.regs[7]);
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4GwesSpinProbe);

#endif  // CERF_DEV_MODE
