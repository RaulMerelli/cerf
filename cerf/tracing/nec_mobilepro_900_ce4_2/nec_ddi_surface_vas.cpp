#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* ddi.dll runs XIP at its link VA, loaded only into gwes -> this user-VA PC is
   unambiguous, no process filter needed. sub_1C11FE0(a1) runs near the end of
   DrvEnablePDEV with R0=the PDEV after its 3 VirtualCopy maps; a1[9]=reg window,
   a1[10]=framebuffer, a1[19]=third region. */
namespace {

class TraceNecDdiSurfaceVas : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&] {
            tm.OnPc(0x01C11FE0u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2u) return;
                const uint32_t a1 = c.regs[0];
                LOG(Trace, "[ddi-pdev] #%u a1=0x%08X reg(a1[9])=0x%08X "
                           "fb(a1[10])=0x%08X third(a1[19])=0x%08X\n",
                    n, a1,
                    c.ReadVa32(a1 + 9u * 4u).value_or(0xDEADBEEFu),
                    c.ReadVa32(a1 + 10u * 4u).value_or(0xDEADBEEFu),
                    c.ReadVa32(a1 + 19u * 4u).value_or(0xDEADBEEFu));
                for (uint32_t i = 0; i < 32u; i += 4u)
                    LOG(Trace, "[ddi-pdev]   a1[%2u..]=%08X %08X %08X %08X\n", i,
                        c.ReadVa32(a1 + (i + 0u) * 4u).value_or(0xDEADBEEFu),
                        c.ReadVa32(a1 + (i + 1u) * 4u).value_or(0xDEADBEEFu),
                        c.ReadVa32(a1 + (i + 2u) * 4u).value_or(0xDEADBEEFu),
                        c.ReadVa32(a1 + (i + 3u) * 4u).value_or(0xDEADBEEFu));
            });

            /* sub_1C13148 @0x1C13244: registry display params just read, before
               the validity check. R7=Width R9=Height R4=Bpp (R5=idx3 R8=idx9
               R10=idx8). Shows whether the registry drives 640x480 or 640x240. */
            tm.OnPc(0x01C13244u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2u) return;
                LOG(Trace, "[ddi-regparams] #%u Width=%d Height=%d Bpp=%d "
                           "idx3=%d idx9=%d idx8=%d\n",
                    n, (int)c.regs[7], (int)c.regs[9], (int)c.regs[4],
                    (int)c.regs[5], (int)c.regs[8], (int)c.regs[10]);
            });

            /* GPE SetMode (vtable[4]=sub_1C17894): creates the primary GPESurf
               (a6&1 path). R0=pdev R2=w R3=h; a6 is at [SP+4]. If this never
               fires, GDI never asked the driver for a surface. */
            tm.OnPc(0x01C17894u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4u) return;
                LOG(Trace, "[ddi-setmode-call] #%u pdev=0x%08X w=%u h=%u "
                           "a5=0x%08X a6=0x%08X LR=0x%08X\n",
                    n, c.regs[0], c.regs[2], c.regs[3],
                    c.ReadVa32(c.regs[13]).value_or(0xDEADBEEFu),
                    c.ReadVa32(c.regs[13] + 4u).value_or(0xDEADBEEFu),
                    c.regs[14]);
            });

            /* SetMode VRAM alloc sub_1C1A420(allocator,w,h): no fire => the
               format check before it rejected; R0=a1[22]==0 => allocator unbuilt. */
            tm.OnPc(0x01C1A420u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4u) return;
                const uint32_t a = c.regs[0];
                LOG(Trace, "[ddi-vramalloc] #%u allocator=0x%08X w=%u h=%u | "
                           "node[0..7]=%08X %08X %08X %08X %08X %08X %08X %08X\n",
                    n, a, c.regs[1], c.regs[2],
                    c.ReadVa32(a + 0u).value_or(0xDEADBEEFu),
                    c.ReadVa32(a + 4u).value_or(0xDEADBEEFu),
                    c.ReadVa32(a + 8u).value_or(0xDEADBEEFu),
                    c.ReadVa32(a + 12u).value_or(0xDEADBEEFu),
                    c.ReadVa32(a + 16u).value_or(0xDEADBEEFu),
                    c.ReadVa32(a + 20u).value_or(0xDEADBEEFu),
                    c.ReadVa32(a + 24u).value_or(0xDEADBEEFu),
                    c.ReadVa32(a + 28u).value_or(0xDEADBEEFu));
            });

            /* GPESurf ctor sub_1C17A3C: reached only if SetMode's mode lookup
               (sub_1C1A420) found the requested mode. R0=new GPESurf (the
               surface handed back), R1=w R2=h. No fire => mode not found =>
               SetMode returns null surface. */
            tm.OnPc(0x01C17A3Cu, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4u) return;
                LOG(Trace, "[ddi-surfctor] #%u gpesurf=0x%08X w=%u h=%u\n",
                    n, c.regs[0], c.regs[1], c.regs[2]);
            });

            /* DrvEnableSurface (sub_1C18864) entry: R0=obj R1=w R2=h R3=fmt. */
            tm.OnPc(0x01C18864u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2u) return;
                LOG(Trace, "[ddi-ensurf] #%u obj=0x%08X w=%u h=%u fmt=%u\n",
                    n, c.regs[0], c.regs[1], c.regs[2], c.regs[3]);
            });
            /* Post GPE SetMode (vtable+0x10): R0 = result, <0 => fail (early
               -1 return, no surface created). */
            tm.OnPc(0x01C188B8u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2u) return;
                LOG(Trace, "[ddi-setmode] #%u result=0x%08X %s\n",
                    n, c.regs[0], (c.regs[0] & 0x80000000u) ? "FAIL" : "ok");
            });
            /* Post engine-callback BL: R0 = surface returned to GDI (success
               path only). */
            tm.OnPc(0x01C188E0u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2u) return;
                const uint32_t v7 = c.regs[0];
                LOG(Trace, "[ddi-surf] #%u v7(surface)=0x%08X *v7=0x%08X "
                           "*(v7+8)=0x%08X\n",
                    n, v7,
                    c.ReadVa32(v7).value_or(0xDEADBEEFu),
                    c.ReadVa32(v7 + 8u).value_or(0xDEADBEEFu));
            });
        });
    }
};

REGISTER_SERVICE(TraceNecDdiSurfaceVas);

}  /* namespace */
