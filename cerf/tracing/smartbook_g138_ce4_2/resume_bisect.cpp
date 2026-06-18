#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include "bundle.h"

#if CERF_DEV_MODE

namespace {

/* SA-1110 sleep-resume bisect. The StartUp head (RCSR read + SMR branch) runs
   MMU-off at runtime PA = link VA + 0x34000000 — hooking its link VA never fires;
   the OAL sleep/resume routines (sub_8C17E090 / sub_8C17E350) run MMU-on at link VA. */
class ResumeBisect : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            auto& tm = emu_.Get<TraceManager>();

            tm.OnPc(0x8C143638u + 0x34000000u, [this](const TraceContext& c) {
                if (rcsr_++ >= 16) return;
                LOG(Jit, "[RESUME] StartUp RCSR read: R10=0x%08X (SMR=bit3) pc=0x%08X\n",
                    c.regs[10], c.pc);
            });
            tm.OnPc(0x8C14366Cu + 0x34000000u, [this](const TraceContext&) {
                if (smr_++ >= 16) return;
                LOG(Jit, "[RESUME] SMR branch taken (sleep-wake resume path)\n");
            });
            tm.OnPc(0x8C143640u + 0x34000000u, [this](const TraceContext& c) {
                if (cold_++ >= 16) return;
                LOG(Jit, "[RESUME] non-SMR path (R10=0x%08X HWR/SWR test)\n", c.regs[10]);
            });

            tm.OnPc(0x8C17CA74u, [this](const TraceContext& c) {
                if (call_++ >= 16) return;
                LOG(Jit, "[RESUME] BL sub_8C17E090 (OAL invoking sleep) lr=0x%08X\n",
                    c.regs[14]);
            });
            tm.OnPc(0x8C17E090u, [this](const TraceContext& c) {
                if (sleep_++ >= 16) return;
                LOG(Jit, "[RESUME] sub_8C17E090 sleep-entry ENTER lr=0x%08X\n", c.regs[14]);
            });
            tm.OnPc(0x8C17E350u, [this](const TraceContext& c) {
                if (resume_++ >= 16) return;
                LOG(Jit, "[RESUME] sub_8C17E350 resume-vector ENTER pc=0x%08X\n", c.pc);
            });
            tm.OnPc(0x8C17CA78u, [this](const TraceContext& c) {
                if (ret_++ >= 16) return;
                LOG(Jit, "[RESUME] returned past sleep call (0x8C17CA78) r0=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x8C17E1FCu, [this](const TraceContext& c) {
                if (fault_++ >= 16) return;
                LOG(Jit, "[RESUME] at fault PC 0x8C17E1FC r0=0x%08X r1=0x%08X r3=0x%08X "
                    "cpsr=0x%08X\n", c.regs[0], c.regs[1], c.regs[3], c.cpsr);
            });
        });
    }

private:
    int rcsr_ = 0, smr_ = 0, cold_ = 0, call_ = 0;
    int sleep_ = 0, resume_ = 0, ret_ = 0, fault_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(ResumeBisect);

#endif  /* CERF_DEV_MODE */
