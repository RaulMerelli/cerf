#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

/* nk.exe exception dispatcher sub_800C2268. At entry R0=context, R1=type,
   R2=FAR, R3=FSR; context offsets PSR=+0x60 SP=+0x98 LR=+0x9C PC=+0xA0 (from
   its PC/Lr/Sp/Psr decompile). Dumps the faulting context to find why a
   thread faults with a garbage SP (the CS0+0x60EC0 frame-store fatal). */
class Falcon4220ExcHandlerContext : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            static std::atomic<uint64_t> hits{0};
            tm.OnPc(0x800C2268u, [](const TraceContext& c) {
                const uint64_t n = hits.fetch_add(1, std::memory_order_relaxed);
                const uint32_t ctx = c.regs[0];
                auto psr = c.ReadVa32(ctx + 0x60u);
                auto sp  = c.ReadVa32(ctx + 0x98u);
                auto lr  = c.ReadVa32(ctx + 0x9Cu);
                auto pc  = c.ReadVa32(ctx + 0xA0u);
                const uint32_t sp_val = sp.has_value() ? *sp : 0xFFFFFFFFu;

                /* Working set of distinct faulting (PC, FAR): host-side dedup so
                   the ~270k/s freeze can't flood. A tiny fixed set = the same
                   instructions re-faulting forever (no forward progress). */
                {
                    const uint32_t fpc = pc.has_value() ? *pc : 0u;
                    const uint32_t far = c.regs[2];
                    const uint32_t key = fpc ^ (far << 1);
                    static std::atomic<uint32_t> seen[64];
                    static std::atomic<uint32_t> seen_n{0};
                    const uint32_t have = seen_n.load(std::memory_order_relaxed);
                    bool fresh = true;
                    for (uint32_t i = 0; i < have && i < 64u; ++i)
                        if (seen[i].load(std::memory_order_relaxed) == key) { fresh = false; break; }
                    if (fresh && have < 64u) {
                        seen[have].store(key, std::memory_order_relaxed);
                        seen_n.store(have + 1u, std::memory_order_relaxed);
                        LOG(Trace, "[EXC-WS] NEW faultPC=0x%08X FAR=0x%08X type=%d curId=0x%08X k=%llu\n",
                            fpc, far, static_cast<int>(c.regs[1]),
                            c.ReadVa32(0xFFFFC808u).value_or(0u),
                            static_cast<unsigned long long>(n));
                    }
                }

                /* Always log the bogus-SP case (target) plus the first 64
                   for context, so the fatal call is never throttled out. */
                if (n >= 64 && sp_val >= 0x00100000u) return;
                LOG(Trace, "[FALCON] exc-handler n=%llu type=%d FAR=0x%08X "
                           "FSR=0x%08X | faultPC=0x%08X faultLR=0x%08X "
                           "faultSP=0x%08X faultPSR=0x%08X | ctx=0x%08X "
                           "handlerLR=0x%08X handlerCPSR=0x%08X\n",
                    static_cast<unsigned long long>(n),
                    static_cast<int>(c.regs[1]), c.regs[2], c.regs[3],
                    pc.has_value()  ? *pc  : 0xDEADBEEFu,
                    lr.has_value()  ? *lr  : 0xDEADBEEFu,
                    sp_val,
                    psr.has_value() ? *psr : 0xDEADBEEFu,
                    ctx, c.regs[14], c.cpsr);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Falcon4220ExcHandlerContext);

#endif  /* CERF_DEV_MODE */
