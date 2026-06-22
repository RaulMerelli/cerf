#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../jit/arm/arm_mmu_state.h"
#include "bundle.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

/* Falcon re-suspend loop. Kernel flag 0xFFFFC886 gates PowerOffSystem: scheduler
   sub_800C2268 calls it at 0x800C254C when the flag is set; sub_800C483C sets it
   when the current thread (0xFFFFC890) state byte (+3) == 2. Watches set/clear +
   each PowerOff call to find why cycle 2 re-suspends where cycle 1 stayed awake. */
class FalconSuspendLoopProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            /* Setter sub_800C483C: log the deciding thread + its state byte. */
            tm.OnPc(0x800C483Cu, [](const TraceContext& c) {
                const uint32_t thr = c.ReadVa32(0xFFFFC890u).value_or(0);
                const uint32_t st  = thr ? c.ReadVa8(thr + 3u).value_or(0xFF) : 0xFF;
                LOG(SocReset, "[SUSLOOP] setter sub_800C483C thr=0x%08X state(+3)=%u "
                    "(==2 sets C886) C886=%u LR=0x%08X\n",
                    thr, st, c.ReadVa8(0xFFFFC886u).value_or(0xFF), c.regs[14]);
            });
            /* PowerOffSystem call site in the scheduler. */
            tm.OnPc(0x800C254Cu, [](const TraceContext& c) {
                LOG(SocReset, "[SUSLOOP] -> PowerOffSystem C886=%u curThr=0x%08X\n",
                    c.ReadVa8(0xFFFFC886u).value_or(0xFF),
                    c.ReadVa32(0xFFFFC890u).value_or(0));
            });
            /* The setter's user-space caller 0x03F87384 (device.exe = FCSE slot 3,
               per paging_livelock_diag): fingerprint the module (code words at the
               PC) and dump the top of stack to trace which routine loops requesting
               suspend. ~once per sleep cycle. */
            const TracePredicate in_devexe = [](const TraceContext& c) {
                return (c.emu.Get<ArmMmu>().State()->process_id >> 25) == 3u;
            };
            tm.OnPcFiltered(0x03F87384u, in_devexe, [](const TraceContext& c) {
                static std::atomic<int> n{0};
                if (n.fetch_add(1, std::memory_order_relaxed) >= 4) return;
                /* Fingerprint the caller's function (code window at 0x03F56610..)
                   to match the extracted module, and walk more stack to reach the
                   loop origin. 0x03F56628 = the caller return addr from stk[+8]. */
                const uint32_t sp = c.regs[13];
                LOG(SocReset, "[SUSLOOP] @0x03F56628 caller-code: "
                    "%08X %08X %08X %08X %08X %08X %08X %08X\n",
                    c.ReadVa32(0x03F56610u).value_or(0), c.ReadVa32(0x03F56614u).value_or(0),
                    c.ReadVa32(0x03F56618u).value_or(0), c.ReadVa32(0x03F5661Cu).value_or(0),
                    c.ReadVa32(0x03F56620u).value_or(0), c.ReadVa32(0x03F56624u).value_or(0),
                    c.ReadVa32(0x03F56628u).value_or(0), c.ReadVa32(0x03F5662Cu).value_or(0));
                LOG(SocReset, "[SUSLOOP] @0x03F87384 proc=0x%08X stk=%08X %08X %08X %08X "
                    "%08X %08X %08X %08X %08X %08X %08X %08X\n",
                    c.ReadVa32(0xFFFFC890u).value_or(0),
                    c.ReadVa32(sp).value_or(0),        c.ReadVa32(sp + 4u).value_or(0),
                    c.ReadVa32(sp + 8u).value_or(0),   c.ReadVa32(sp + 12u).value_or(0),
                    c.ReadVa32(sp + 16u).value_or(0),  c.ReadVa32(sp + 20u).value_or(0),
                    c.ReadVa32(sp + 24u).value_or(0),  c.ReadVa32(sp + 28u).value_or(0),
                    c.ReadVa32(sp + 32u).value_or(0),  c.ReadVa32(sp + 36u).value_or(0),
                    c.ReadVa32(sp + 40u).value_or(0),  c.ReadVa32(sp + 44u).value_or(0));
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconSuspendLoopProbe);

#endif  /* CERF_DEV_MODE */
