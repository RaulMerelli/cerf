#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../socs/sa11xx/sa11xx_intc.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* 0x8005591C = kernel IRQ exception handler entry (regs[14]=LR_irq=interrupted
   PC+4). 0x8005594C = return point after OEMInterruptHandler (r0=SYSINTR; 0 =
   spurious). Throttled dump of the INTC pending/mask/IRQ-line state names a
   stale-across-reset source if the handler storms. */
class ResetIrqStormProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            auto& tm = emu_.Get<TraceManager>();

            tm.OnPc(0x8005591Cu, [this](const TraceContext& c) {
                ++entries_;
                if (entries_ - last_logged_ < 200000u && entries_ > 1) return;
                last_logged_ = entries_;
                auto& intc = c.emu.Get<Sa11xxIntc>();
                LOG(Trace, "[IRQSTORM] entry #%u interrupted_pc=0x%08X "
                    "ICIP=0x%08X ICPR=0x%08X ICMR=0x%08X ICLR=0x%08X\n",
                    entries_, c.regs[14] - 4u, intc.GetIcIp(), intc.GetIcpr(),
                    intc.GetIcmr(), intc.GetIclr());
            });

            tm.OnPc(0x8005594Cu, [this](const TraceContext& c) {
                const uint32_t sysintr = c.regs[0];
                if (sysintr == last_sysintr_) return;
                last_sysintr_ = sysintr;
                LOG(Trace, "[IRQSTORM] OEMIH returned SYSINTR=%u\n", sysintr);
            });
        });
    }

private:
    uint32_t entries_      = 0;
    uint32_t last_logged_  = 0;
    uint32_t last_sysintr_ = 0xFFFFFFFFu;
};

}  /* namespace */

REGISTER_SERVICE(ResetIrqStormProbe);

#endif  /* CERF_DEV_MODE */
