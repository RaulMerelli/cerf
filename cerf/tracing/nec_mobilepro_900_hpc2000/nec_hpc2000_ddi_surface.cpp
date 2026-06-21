#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* ddi.dll is XIP at its link VA and loaded only into gwes, so these user-VA
   hooks fire for one process only - no filter needed. sub_11B1E84 = GPE ctor
   tail, R0 = built GPE object; dump its VirtualCopy'd VAs + mode index to see
   if the framebuffer/surface VA (gwes faults on 0x06000000) is mapped. */
namespace {

class TraceNecHpc2000DdiSurface : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Hpc2000BundleCrc32, [&] {
            tm.OnPc(0x011B1E84u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4) return;
                const uint32_t o = c.regs[0];  /* GPE object. */
                const uint32_t regs_va = c.ReadVa32(o + 36u).value_or(0xDEADBEEFu);
                const uint32_t fb_va   = c.ReadVa32(o + 40u).value_or(0xDEADBEEFu);
                const uint32_t third_va= c.ReadVa32(o + 76u).value_or(0xDEADBEEFu);
                const uint32_t mode_idx= c.ReadVa32(o + 184u).value_or(0xDEADBEEFu);
                const uint32_t fb_size = c.ReadVa32(o + 84u).value_or(0xDEADBEEFu);
                const uint32_t lowmem  = c.ReadVa32(o + 216u).value_or(0xDEADBEEFu);
                LOG(Trace, "[ddi-gpe] #%u obj=0x%08X mode_idx=%u regs_va=0x%08X "
                           "fb_va=0x%08X(*=0x%08X) third_va=0x%08X fb_size=0x%X "
                           "lowmem=0x%08X\n",
                    n, o, mode_idx, regs_va, fb_va,
                    c.ReadVa32(fb_va).value_or(0xDEADBEEFu), third_va, fb_size,
                    lowmem);
            });

            /* DrvEnablePDEV entry: a1 (R0) = the GDIINFO/mode params block. */
            tm.OnPc(0x011B8594u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2) return;
                LOG(Trace, "[ddi-pdev] #%u a1=0x%08X w=%d h=%d bpp_idx=%d\n",
                    n, c.regs[0],
                    (int)c.ReadVa32(c.regs[0] + 43u * 4u).value_or(0u),
                    (int)c.ReadVa32(c.regs[0] + 44u * 4u).value_or(0u),
                    (int)c.ReadVa32(c.regs[0] + 42u * 4u).value_or(0u));
            });

            /* Pin which sub_11B8594 GPE-init gate makes DrvEnablePDEV return 0
               (gwes-side [gwes-pdev]=0). 0x11B85E8 vtable[40] is signed, <0=fail. */
            tm.OnPc(0x011B85B4u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2) return;
                LOG(Trace, "[ddi-gpe-new] #%u sub_11B1B48 gpe(R0)=0x%08X\n",
                    n, c.regs[0]);
            });
            tm.OnPc(0x011B85C4u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2) return;
                LOG(Trace, "[ddi-gpe-init] #%u sub_11BB17C(R0)=0x%08X\n",
                    n, c.regs[0]);
            });
            tm.OnPc(0x011B85E8u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2) return;
                LOG(Trace, "[ddi-gpe-vt40] #%u vtable[40](R0)=%d\n",
                    n, (int)c.regs[0]);
            });

            /* vtable[40]'s E_OUTOFMEMORY is sub_11B6F30 (surface alloc). 0x11B6F48
               R3 = GPE[15] (non-zero => second VRAM surface rejected), R2 = width,
               R8 = height; 0x11B6FA4 R0 = sub_11B9330 VRAM-alloc result (0=fail). */
            tm.OnPc(0x011B6F48u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 6) return;
                LOG(Trace, "[ddi-surf-alloc] #%u gpe=0x%08X gpe[15]=0x%08X "
                           "w=%d h=%d\n",
                    n, c.regs[4], c.regs[3], (int)c.regs[2], (int)c.regs[8]);
            });
            tm.OnPc(0x011B6FA4u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 6) return;
                LOG(Trace, "[ddi-vram-alloc] #%u sub_11B9330(R0)=0x%08X\n",
                    n, c.regs[0]);
            });

            /* The VRAM allocator is built too small for 640x480 when the gate
               *(LowMemory+0x1A858) is 0 (sub_11B24DC: if(!*v15) v18=height+1=241).
               0x11B25E4 R3 = gate value, R6 = gate VA; sub_11B90F0 R1/R2 = the
               allocator width/height it is actually built with. */
            tm.OnPc(0x011B25E4u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2) return;
                LOG(Trace, "[ddi-lowmem-gate] #%u *(0x%08X)=0x%08X\n",
                    n, c.regs[6], c.regs[3]);
            });
            tm.OnPc(0x011B90F0u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 3) return;
                LOG(Trace, "[ddi-alloc-build] #%u sub_11B90F0 w(R1)=%d h(R2)=%d\n",
                    n, (int)c.regs[1], (int)c.regs[2]);
            });
        });
    }
};

REGISTER_SERVICE(TraceNecHpc2000DdiSurface);

}  /* namespace */
