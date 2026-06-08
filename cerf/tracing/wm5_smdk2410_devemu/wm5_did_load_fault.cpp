#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

/* Captures the kernel memcpy (sub_800B3660; R0=dest R1=src R2=size) that faults
   at slot-3 0x06000000 while device.exe loads the driver-in-driver carrier. */

namespace {

class TraceWm5DidLoadFault : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&tm] {
            tm.OnPcFiltered(
                0x800B36C8u,
                [](const TraceContext& c) {
                    return c.regs[0] >= 0x05FFF000u && c.regs[0] < 0x06001000u;
                },
                [](const TraceContext& c) {
                    LOG(Trace, "[DIDFAULT] memcpy@loop dest(R0)=0x%08X src(R1)=0x%08X "
                               "remain(R2)=0x%X LR=0x%08X\n",
                        c.regs[0], c.regs[1], c.regs[2], c.regs[14]);
                });
        });
    }
};

REGISTER_SERVICE(TraceWm5DidLoadFault);

}  /* namespace */
