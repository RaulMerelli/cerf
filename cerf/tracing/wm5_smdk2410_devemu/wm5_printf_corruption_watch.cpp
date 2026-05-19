#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

class TraceWm5PrintfCorruption : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&tm] {
            tm.OnPc(0x800AFB50u, [](const TraceContext& c) {
                LOG(Trace, "[PRINTF_CALL] reserving-printf "
                           "R0(fmt)=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                           "SP=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[13], c.regs[14]);
            });
            /* Also hook the LDR R0,=fmt and LDR R1,[LR] / LDR R2,[R1] sites
               to see the intermediate state. */
            tm.OnPc(0x800AFB3Cu, [](const TraceContext& c) {
                LOG(Trace, "[PRINTF_PREP] @AFB3C(LDR R0=fmt) "
                           "R1=0x%08X R2=0x%08X R3=0x%08X R4=0x%08X "
                           "R8=0x%08X LR=0x%08X\n",
                    c.regs[1], c.regs[2], c.regs[3], c.regs[4],
                    c.regs[8], c.regs[14]);
            });
            tm.OnPc(0x800AFB4Cu, [](const TraceContext& c) {
                LOG(Trace, "[PRINTF_LDR_R1] @AFB4C(LDR R1=[LR]) "
                           "R0=0x%08X LR=0x%08X *(LR)=0x%08X\n",
                    c.regs[0], c.regs[14],
                    c.ReadVa32(c.regs[14]).value_or(0xDEADBEEFu));
            });
        });
    }
};

REGISTER_SERVICE(TraceWm5PrintfCorruption);

}  /* namespace */
