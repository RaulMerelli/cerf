#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include "bundle.h"

#if CERF_DEV_MODE

namespace {

/* Bisects the SA-1110 kernel MMU-enable transition (nk.exe sub_8C144BA8 tail).
   Cold-boot runs MMU-off at PA = link-VA + 0x34000000 (0x8C140000 -> PA
   0xC0140000), so pre-MMU insns hook at their PA; the post-MMU continuation
   (0x8C144D40) hooks at its VA and fires only if the SCTLR enable took. */
class MmuEnableBisect : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            auto& tm = emu_.Get<TraceManager>();

            /* MMU-off code runs at PA = link VA + 0x34000000 (linked
               0x8C140000, placed PA 0xC0140000). */
            auto hook_pa = [&](uint32_t va, const char* name) {
                tm.OnPc(va + 0x34000000u, [this, name](const TraceContext& c) {
                    if (fired_++ >= 64) return;
                    LOG(Jit, "[MMUBIS] %s (MMU-off PA) pc=0x%08X lr=0x%08X "
                        "r0=0x%08X r10=0x%08X\n",
                        name, c.pc, c.regs[14], c.regs[0], c.regs[10]);
                });
            };
            auto hook_va = [&](uint32_t va, const char* name) {
                tm.OnPc(va, [this, name](const TraceContext& c) {
                    if (fired_++ >= 64) return;
                    LOG(Jit, "[MMUBIS] %s (MMU-on VA) pc=0x%08X lr=0x%08X "
                        "r0=0x%08X\n", name, c.pc, c.regs[14], c.regs[0]);
                });
            };

            hook_pa(0x8C14361Cu, "entry");
            hook_pa(0x8C1438D8u, "BL sub_8C144BA8 (cold->MMU setup)");
            hook_pa(0x8C144BA8u, "sub_8C144BA8 entry");
            hook_pa(0x8C144CF0u, "page table built (BL copy-vectors)");
            hook_pa(0x8C144D2Cu, "LDR R0,=loc_8C144D40 (pre-enable)");
            hook_pa(0x8C144D34u, "MCR c1 SCTLR=MMU-enable");
            hook_pa(0x8C144D38u, "MOV PC,R0 (jump to virtual)");
            hook_va(0x8C144D40u, "POST-MMU landed");
            hook_va(0x8C144D5Cu, "POST-MMU MSR CPSR=0xD7");
        });
    }

private:
    int fired_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(MmuEnableBisect);

#endif  /* CERF_DEV_MODE */
