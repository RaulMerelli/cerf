#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "bundle.h"

#if CERF_DEV_MODE

namespace {

uint32_t Slot(const TraceContext& c) {
    return c.emu.Get<ArmMmu>().State()->process_id >> 25;
}

/* DOC ID has exactly one writer (WarmCheck sub_11048), so R0 here vs the next
   cycle's sub_110B4 read separates "writes don't survive the reboot" (R0=3,
   reads back 0) from "DOC-type flag 0x822AFE30 isn't 3" (R0=0). */
class FalconWarmCheckDocIdWrite : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            TracePredicate warmcheck = [](const TraceContext& c) { return Slot(c) == 4u; };
            tm.OnPcFiltered(0x11048u, warmcheck, [](const TraceContext& c) {
                LOG(Trace, "[FALCON-DW] WarmCheck writes DOC ID=%d (R0=0x%08X) slot=%u\n",
                    static_cast<int>(c.regs[0]), c.regs[0], Slot(c));
            });
            tm.OnPcFiltered(0x11000u, warmcheck, [](const TraceContext& c) {
                LOG(Trace, "[FALCON-DW] WarmCheck -> IOCTL_HAL_REBOOT slot=%u LR=0x%08X\n",
                    Slot(c), c.regs[14]);
            });
            tm.OnRunLoopIter([last = uint32_t(0xFFFFFFFFu)](const TraceContext& c) mutable {
                if (auto v = c.ReadVa32(0x822AFE30u); v && *v != last) {
                    last = *v;
                    LOG(Trace, "[FALCON-DW] 0x822AFE30 (DOC-type flag) = %u\n", *v);
                }
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconWarmCheckDocIdWrite);

#endif  /* CERF_DEV_MODE */
