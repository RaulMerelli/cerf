#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* gwes.exe code runs only in gwes, so these user-VA hooks need no filter.
   PeekVaToHost can't read gwes's FCSE data (the TLB-cached page diverges from
   the walk), so read REGISTERS at the load sites: 0x5528C R3=device+48 (the
   cached-vs-stock selector vs R2=type); 0x55298 R0=device+192 (the faulting ptr). */
namespace {

class TraceNecHpc2000GwesSurface : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Hpc2000BundleCrc32, [&] {
            tm.OnPc(0x0005528Cu, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 8) return;
                LOG(Trace, "[gwes-sel] #%u type(R2)=0x%08X dev+48(R3)=0x%08X "
                           "dev(R1)=0x%08X cached=%d\n",
                    n, c.regs[2], c.regs[3], c.regs[1],
                    c.regs[2] == c.regs[3]);
            });

            tm.OnPc(0x00055298u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 8) return;
                LOG(Trace, "[gwes-surf] #%u dev+192(R0)=0x%08X dev(R1)=0x%08X\n",
                    n, c.regs[0], c.regs[1]);
            });

            /* sub_20588(bpp)->format: R0 = DEVINFO[6] the driver reported. A
               non-bpp R0 returns 0, which makes device+48=0 and forces the
               null cached-surface path. */
            tm.OnPc(0x00020588u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 12) return;
                LOG(Trace, "[gwes-fmt] #%u sub_20588 arg(R0=DEVINFO[6])=0x%08X "
                           "LR=0x%08X\n", n, c.regs[0], c.regs[14]);
            });

            /* sub_4CEEC PDEV-enable, where the screen device should get +48/+192.
               Entry (R0=dev, R1=DDI.DLL handle); 0x4D114 R3 = DrvEnablePDEV result
               *(dev+0xA4); 0x4D148 R0 = sub_2CCA4 result (BNE !=5 -> exit). One of
               these gates exits before sub_20588 (which never fires). */
            tm.OnPc(0x0004CEECu, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4) return;
                LOG(Trace, "[gwes-enable] #%u dev(R0)=0x%08X driver(R1)=0x%08X\n",
                    n, c.regs[0], c.regs[1]);
            });
            tm.OnPc(0x0004D114u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4) return;
                LOG(Trace, "[gwes-pdev] #%u DrvEnablePDEV result(R3)=0x%08X\n",
                    n, c.regs[3]);
            });
            tm.OnPc(0x0004D148u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4) return;
                LOG(Trace, "[gwes-cca4] #%u sub_2CCA4 result(R0)=%d (need 5)\n",
                    n, (int)c.regs[0]);
            });
        });
    }
};

REGISTER_SERVICE(TraceNecHpc2000GwesSurface);

}  /* namespace */
