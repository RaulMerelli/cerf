#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../socs/sa1110/sa1110_intc.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* serial.dll's IST spins reading its interrupt-status register via SWP
   (sub_F93B68 = __swp(0, addr)). Logs the polled address (r0) + the live
   value at it, to name which port/register stays "interrupt pending" and
   wedges the IST -> kernel TLB-miss storm -> desktop never renders. */
class SerialPollProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            /* sub_F91FFC entry: a1=r0. a1+0xC0 -> modem/line-status reg base,
               a1+0xC8 -> the 0xC00 status base. Dump both + the stuck reg. */
            emu_.Get<TraceManager>().OnPc(0xF91FFCu, [this](const TraceContext& c) {
                const uint32_t a1 = c.regs[0];
                const uint32_t baseB = c.ReadVa32(a1 + 0xC0).value_or(0);
                const uint32_t baseC = c.ReadVa32(a1 + 0xC8).value_or(0);
                /* baseB+0x20 is the uncached alias of the SA-1110 INTC ICPR
                   (PA 0x90050020). Read the live value from the INTC. */
                const uint32_t icpr = c.emu.Get<Sa1110Intc>().GetIcpr();
                if (icpr == lastV_) return;   /* throttle on ICPR change. */
                lastB_ = baseB; lastV_ = icpr;
                LOG(Trace, "[SERPOLL] a1=0x%08X baseB=0x%08X baseC=0x%08X "
                    "ICPR=0x%08X (Ser3=%d DMA4=%d DMA5=%d)\n", a1, baseB, baseC,
                    icpr, !!(icpr & 0x20000), !!(icpr & 0x1000000),
                    !!(icpr & 0x2000000));
            });
        });
    }

private:
    uint32_t lastB_ = 0xFFFFFFFFu;
    uint32_t lastV_ = 0xFFFFFFFFu;
};

}  /* namespace */

REGISTER_SERVICE(SerialPollProbe);

#endif  /* CERF_DEV_MODE */
