#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_cpu.h"
#include "../../jit/cpu_state.h"
#include "bundle.h"

#include <cstdint>

/* Idle/tick-IRQ deadlock probe: OEMIdle WFI 0x90242000, IRQ vector 0x902058D8,
   tick ISR 0x9023B758, OST dispatch 0x9023E8A0. */
namespace {

class TraceNecIdleTickDeadlock : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&] {
            /* OEMIdle WFI body. Sample early + periodic; the parked-state fields
               are the answer: CPSR.I (bit7) and irq_interrupt_pending. */
            tm.OnPc(0x90242000u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 30 || (n % 5000) == 0) {
                    auto* st = c.emu.Get<ArmCpu>().State();
                    LOG(Trace, "[idle-wfi] #%u cpsr=0x%08X I=%u irq_pend=%u "
                               "cycles=0x%08X\n",
                        n, c.cpsr, (c.cpsr >> 7) & 1u,
                        st->irq_interrupt_pending, st->guest_cycle_counter);
                }
            });

            /* IRQ vector handler 0x902058D8. */
            tm.OnPc(0x902058D8u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 8 || (n % 1000) == 0)
                    LOG(Trace, "[irq-vec] #%u cpsr=0x%08X LR=0x%08X\n",
                        n, c.cpsr, c.regs[14]);
            });

            /* OST tick ISR sub_9023B758 (clears OSSR, re-arms OSMR0). */
            tm.OnPc(0x9023B758u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 8 || (n % 1000) == 0)
                    LOG(Trace, "[tick-isr] #%u LR=0x%08X\n", n, c.regs[14]);
            });

            /* OST interrupt dispatch (caller of the tick ISR). */
            tm.OnPc(0x9023E8A0u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 8 || (n % 1000) == 0)
                    LOG(Trace, "[ost-dispatch] #%u R0=0x%08X LR=0x%08X\n",
                        n, c.regs[0], c.regs[14]);
            });
        });
    }
};

REGISTER_SERVICE(TraceNecIdleTickDeadlock);

}  /* namespace */
