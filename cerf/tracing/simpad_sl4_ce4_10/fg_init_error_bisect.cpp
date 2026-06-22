#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "bundle.h"

#include <atomic>
#include <memory>

#if CERF_DEV_MODE

namespace {

/* psmfsd FG_Init worker sub_12A45A8 returns c2 (194). Capture R0 at each
   post-call site (the sub's return value is live there) to find which sub
   returns 194. Unfiltered + pid logged: psmfsd's process is not resolved, so
   fires are attributed by pid (discovery). */
class SimpadSl4FgInitErrorBisect : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            Ret(tm, 0x12A4614u, "sub_12ACB08 identify#1");
            Ret(tm, 0x12A4668u, "sub_12ACB08 identify#2");
            Ret(tm, 0x12A471Cu, "sub_12AC9B8 anchor#1");
            Ret(tm, 0x12A4740u, "sub_12AC9B8 anchor#2");
            Ret(tm, 0x12A47ACu, "sub_12A5E74 format1");
            Ret(tm, 0x12A47E0u, "sub_12A5C24 format2");
            Ret(tm, 0x12A47FCu, "FG_Init return (R4)");
            /* drill sub_12ACB08 -> sub_12AE518 -> sub_12A2D90 internals */
            Ret(tm, 0x12A2E48u, "sub_12A7B30 geom-register");
            Ret(tm, 0x12A2E74u, "sub_12A7E30 region#1");
            Ret(tm, 0x12A2E94u, "sub_12A7E30 region#2");
            Ret(tm, 0x12A2EB8u, "sub_12A7F78 flash-read");
            /* drill sub_12A7B30: media-handler init vs the off_12BB1A0 probe chain */
            Ret(tm, 0x12A7C70u, "media-handler init");
            tm.OnPc(0x12A7C98u, [](const TraceContext& c) {
                auto fn = c.ReadVa32(c.regs[4]);
                LOG(Trace, "[FGRET] probe result r0=%d (0x%X) tbl=0x%08X fn=0x%08X "
                           "pid=0x%08X\n", int32_t(c.regs[0]), c.regs[0], c.regs[4],
                    fn ? *fn : 0u, c.emu.Get<ArmMmu>().State()->process_id);
            });
            /* sub_12AD330 per-block lock read: R0 = block_base+8 (read-ID mode);
               capture the address + the value CERF returns there (bit0 = lock). */
            auto n = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0x12AD858u, [n](const TraceContext& c) {
                if (n->fetch_add(1) >= 12u) return;
                auto v = c.ReadVa32(c.regs[0]);
                LOG(Trace, "[FGRET] lockread addr=0x%08X val=0x%08X bit0=%d pid=0x%08X\n",
                    c.regs[0], v ? *v : 0xDEADBEEFu, v ? int(*v & 1u) : -1,
                    c.emu.Get<ArmMmu>().State()->process_id);
            });
        });
    }

private:
    void Ret(TraceManager& tm, uint32_t va, const char* tag) {
        tm.OnPc(va, [tag](const TraceContext& c) {
            LOG(Trace, "[FGRET] %s r0=%d (0x%X) pid=0x%08X lr=0x%08X\n",
                tag, int32_t(c.regs[0]), c.regs[0],
                c.emu.Get<ArmMmu>().State()->process_id, c.regs[14]);
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4FgInitErrorBisect);

#endif  // CERF_DEV_MODE
