#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* Bisects the VFP feature-probe UND halt: vector 0xFFFF0004 -> UND prologue
   0x90205408 -> C dispatcher sub_9020912C(ctx,type,FAR,FSR) -> resume at
   0x90205C80. ctx offsets PSR+0x60 SP+0x98 LR+0x9C PC+0xA0. */
namespace {

class TraceNecVfpUndPath : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&] {
            /* (1) UND vector. One-shot dump of the runtime handler pointer
               table 0xFFFF03E0..0xFFFF03FC (reset/und/swi/pabt/dabt/rsvd/
               irq/fiq) so we see the actual UND handler at 0xFFFF03E4. */
            tm.OnPc(0xFFFF0004u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 3) {
                    LOG(Trace, "[und-vec] hit #%u LR_und=0x%08X cpsr=0x%08X\n",
                        n, c.regs[14], c.cpsr);
                    for (uint32_t a = 0xFFFF03E0u; a <= 0xFFFF03FCu; a += 4) {
                        LOG(Trace, "[und-vec] ptr@0x%08X = 0x%08X\n",
                            a, c.ReadVa32(a).value_or(0xDEADBEEFu));
                    }
                    const uint32_t h = c.ReadVa32(0xFFFF03E4u).value_or(0u);
                    LOG(Trace, "[und-vec] UND handler ptr=0x%08X firstInsn=0x%08X\n",
                        h, c.ReadVa32(h).value_or(0xDEADBEEFu));
                } else if ((n % 1000) == 0) {
                    LOG(Trace, "[und-vec] hit #%u (still firing)\n", n);
                }
            });

            /* (2) UND handler prologue (0x90205408). */
            tm.OnPc(0x90205408u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 5)
                    LOG(Trace, "[und-prologue] hit #%u SP=0x%08X LR=0x%08X\n",
                        n, c.regs[13], c.regs[14]);
            });

            /* (3) C dispatcher sub_9020912C(ctx, type, FAR, FSR). */
            tm.OnPc(0x9020912Cu, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 8) {
                    const uint32_t ctx = c.regs[0];
                    LOG(Trace, "[dispatch] hit #%u type=%u FAR=0x%08X FSR=0x%08X "
                               "ctx=0x%08X ctxPC=0x%08X ctxPSR=0x%08X LR=0x%08X\n",
                        n, c.regs[1], c.regs[2], c.regs[3], ctx,
                        c.ReadVa32(ctx + 0xA0u).value_or(0xDEADBEEFu),
                        c.ReadVa32(ctx + 0x60u).value_or(0xDEADBEEFu),
                        c.regs[14]);
                }
            });

            /* (4) Post-FMRX resume (0x90205C80) — the success signal: if this
               fires, the kernel fixed up the UND and the probe continued. */
            tm.OnPc(0x90205C80u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 5)
                    LOG(Trace, "[probe-resume] hit #%u (FMRX returned) R0=0x%08X\n",
                        n, c.regs[0]);
            });

            /* (5) VFP probe entry (0x90208D04). */
            tm.OnPc(0x90208D04u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 5)
                    LOG(Trace, "[vfp-probe] entry #%u LR=0x%08X\n", n, c.regs[14]);
            });
        });
    }
};

REGISTER_SERVICE(TraceNecVfpUndPath);

}  /* namespace */
