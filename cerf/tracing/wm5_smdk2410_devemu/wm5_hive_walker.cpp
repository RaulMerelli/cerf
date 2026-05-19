#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

class TraceWm5HiveWalker : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            tm.OnPc(0x225B8u, [](const TraceContext& c) {
                LOG(Trace, "[HIVE_WALK_ENTRY] pc=0x%08X "
                           "R0=0x%08X R1=0x%08X R3=0x%08X R5=0x%08X "
                           "R6=0x%08X R7=0x%08X R8=0x%08X R9=0x%08X "
                           "R10=0x%08X R11=0x%08X LR=0x%08X\n",
                    c.pc,
                    c.regs[0], c.regs[1], c.regs[3], c.regs[5],
                    c.regs[6], c.regs[7], c.regs[8], c.regs[9],
                    c.regs[10], c.regs[11], c.regs[14]);
            });
        });
    }
};

REGISTER_SERVICE(TraceWm5HiveWalker);

}  /* namespace */
