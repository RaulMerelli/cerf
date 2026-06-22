#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../jit/arm/arm_mmu_state.h"
#include "bundle.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

/* pm.dll sub_3F55E7C = the CE4.20 PM activity-timer state machine. R0 = event code
   (1 = user activity -> On; 5 = timeout tick; 6/7 or R1==-1 = force toward suspend),
   R1 = elapsed ms, global 0x1FFB254 = current state (0 On, 2 useridle, 3 systemidle,
   5 suspend). Without this decode the [PMACT] numbers are meaningless. */
class FalconPmActivityState : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            /* pm.dll runs in device.exe; admit only its address space (slot 3). */
            const TracePredicate in_devexe = [](const TraceContext& c) {
                return (c.emu.Get<ArmMmu>().State()->process_id >> 25) == 3u;
            };
            tm.OnPcFiltered(0x3F55E7Cu, in_devexe, [](const TraceContext& c) {
                static std::atomic<int> n{0};
                if (n.fetch_add(1, std::memory_order_relaxed) >= 300) return;
                LOG(SocReset, "[PMACT] sub_3F55E7C event(R0)=%u elapsed(R1)=%d "
                    "state(0x1FFB254)=%d LR=0x%08X\n",
                    c.regs[0], static_cast<int>(c.regs[1]),
                    static_cast<int>(c.ReadVa32(0x1FFB254u).value_or(0xFFFFFFFFu)),
                    c.regs[14]);
            });
            /* sub_3F52960 = SetSystemPowerState(R0=state-name ptr). Catches every
               suspend request (external + activity path); [PMACT] sees only the
               timer path. LR names the post-wake requester. */
            tm.OnPcFiltered(0x3F52960u, in_devexe, [](const TraceContext& c) {
                static std::atomic<int> n{0};
                if (n.fetch_add(1, std::memory_order_relaxed) >= 200) return;
                char name[24] = {0};
                const uint32_t p = c.regs[0];
                if (p) {
                    for (int i = 0; i < 23; ++i) {
                        const uint32_t ch = c.ReadVa16(p + i * 2u).value_or(0);
                        if (ch == 0u || ch > 0x7Eu) break;
                        name[i] = static_cast<char>(ch);
                    }
                }
                LOG(SocReset, "[PMSSPS] SetSystemPowerState name='%s' R0=0x%08X "
                    "R1=0x%08X R2=0x%08X LR=0x%08X\n",
                    name, c.regs[0], c.regs[1], c.regs[2], c.regs[14]);
            });
            /* coredll!SetSystemPowerState (XIP, runs in the caller's process).
               INTENTIONALLY UNFILTERED: the suspend requester's process is what
               we're identifying, so the hook must fire for ALL processes; pCurProc
               (0xFFFFC890) read inside names whichever one called. */
            tm.OnPc(0x3FA1968u, [](const TraceContext& c) {
                if ((c.regs[1] & 0x200000u) == 0u) return;   /* SUSPEND only */
                static std::atomic<int> n{0};
                if (n.fetch_add(1, std::memory_order_relaxed) >= 100) return;
                LOG(SocReset, "[SUSREQ] SetSystemPowerState(SUSPEND) caller "
                    "proc=0x%08X LR=0x%08X R0=0x%08X R1=0x%08X\n",
                    c.ReadVa32(0xFFFFC890u).value_or(0), c.regs[14],
                    c.regs[0], c.regs[1]);
            });
            /* OAL battery-type detect epilogue (nk.exe sub_800F8E24, kernel VA):
               R0 = detected type. 0 = no main battery (the GPIO73 bit-banged smart
               battery never responded), 1/2 = present. */
            tm.OnPc(0x800F8EBCu, [](const TraceContext& c) {
                static std::atomic<int> n{0};
                if (n.fetch_add(1, std::memory_order_relaxed) >= 40) return;
                LOG(SocReset, "[BATTYPE] OAL battery-type detect = %d "
                    "(0=main battery absent, 1/2=present)\n",
                    static_cast<int>(c.regs[0]));
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconPmActivityState);

#endif  /* CERF_DEV_MODE */
