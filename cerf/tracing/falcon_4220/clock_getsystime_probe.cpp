#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

/* GetSystemTime syscall sub_800C4004: R4 = SYSTEMTIME* output, preserved; at
   0x800C4038 (after sub_800F5950 fills it) [R4] holds the returned wall-clock.
   Frozen here while the CPL date races => race is above this, in user space. */
class FalconClockGetSysTimeProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            tm.OnPc(0x800C4038u, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                const uint32_t k = n.fetch_add(1, std::memory_order_relaxed);
                if (k >= 40u && (k & 0x1Fu) != 0u) return;
                const uint32_t st = c.regs[4];   /* R4 = SYSTEMTIME* */
                const uint32_t yr = c.ReadVa16(st).value_or(0xFFFF);
                const uint32_t mo = c.ReadVa16(st + 2u).value_or(0xFFFF);
                const uint32_t dy = c.ReadVa16(st + 6u).value_or(0xFFFF);
                const uint32_t hr = c.ReadVa16(st + 8u).value_or(0xFFFF);
                const uint32_t mi = c.ReadVa16(st + 10u).value_or(0xFFFF);
                const uint32_t se = c.ReadVa16(st + 12u).value_or(0xFFFF);
                LOG(SocReset, "[GETSYS] n=%u %04u-%02u-%02u %02u:%02u:%02u "
                    "caller(LR)=0x%08X curId=0x%08X\n",
                    k, yr, mo, dy, hr, mi, se, c.regs[14],
                    c.ReadVa32(0xFFFFC808u).value_or(0u));
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconClockGetSysTimeProbe);

#endif  /* CERF_DEV_MODE */
