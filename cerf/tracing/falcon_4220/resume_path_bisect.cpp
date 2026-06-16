#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

/* Set when the OS resume routine sub_800F76A0 is reached, so the 0x0C abort-
   vector hook only logs the post-resume fault and not an unrelated early-boot
   abort. */
std::atomic<bool> g_resumed{false};

/* Tracks the tail of the OS resume routine sub_800F76A0 (Falcon PXA255 deep
   sleep): the last hook to fire names where the restore dies. */
class Falcon4220ResumePathBisect : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            tm.OnPc(0x800F76A0u, [](const TraceContext& c) {
                g_resumed.store(true, std::memory_order_release);
                LOG(SocReset, "[RESUME] sub_800F76A0 entry cpsr=0x%08X\n", c.cpsr);
            });
            /* MSR CPSR_c: about to restore the suspended-mode CPSR (acc0 + cp14
               restore already done by here). R0 = restored CPSR. */
            tm.OnPc(0x800F789Cu, [](const TraceContext& c) {
                LOG(SocReset, "[RESUME] restore CPSR_c=0x%08X (cur cpsr=0x%08X)\n",
                    c.regs[0], c.cpsr);
            });
            tm.OnPc(0x800F78B4u, [](const TraceContext& c) {
                LOG(SocReset, "[RESUME] sub_800F5540 returned cpsr=0x%08X\n", c.cpsr);
            });
            tm.OnPc(0x800F78C8u, [](const TraceContext& c) {
                LOG(SocReset, "[RESUME] sub_800F4EAC returned cpsr=0x%08X\n", c.cpsr);
            });
            tm.OnPc(0x800F78CCu, [](const TraceContext& c) {
                LOG(SocReset, "[RESUME] sub_800F4EC4 returned cpsr=0x%08X\n", c.cpsr);
            });
            /* POP {R0-R12,LR}: about to restore the suspended context from SP. */
            tm.OnPc(0x800F78D0u, [](const TraceContext& c) {
                LOG(SocReset, "[RESUME] sub_800F50E8 returned, POP ctx SP=0x%08X cpsr=0x%08X\n",
                    c.regs[13], c.cpsr);
            });
            /* BX LR: the return into the running OS. R14 = the resume target. */
            tm.OnPc(0x800F78D4u, [](const TraceContext& c) {
                LOG(SocReset, "[RESUME] BX LR into OS: target(R14)=0x%08X SP=0x%08X cpsr=0x%08X\n",
                    c.regs[14], c.regs[13], c.cpsr);
            });
            /* Post-resume freeze: the OS runs ~1s then wedges at 0x0C. Gated on
               g_resumed so a pre-resume abort can't consume the cap. R14 names
               where the bad branch/return came from. */
            tm.OnPc(0x0000000Cu, [](const TraceContext& c) {
                if (!g_resumed.load(std::memory_order_acquire)) return;
                static std::atomic<int> n{0};
                if (n.fetch_add(1, std::memory_order_relaxed) >= 4) return;
                LOG(SocReset, "[RESUME] @0x0C LR(R14)=0x%08X SP=0x%08X cpsr=0x%08X "
                    "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X\n",
                    c.regs[14], c.regs[13], c.cpsr,
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3]);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Falcon4220ResumePathBisect);

#endif  /* CERF_DEV_MODE */
