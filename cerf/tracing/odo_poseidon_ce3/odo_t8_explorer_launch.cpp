#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"
#include "../../jit/arm_mmu_state.h"
#include "odo_bundle.h"

namespace {

/* T8: captures every coredll!CreateProcessW call (shared DLL region, one VA for
   all processes; process_id names the caller) to diff explorer's launch of the
   card .exe - which prefetch-aborts - against the gwes self-test's, which works. */
constexpr uint32_t kCreateProcessW = 0x01F9B5B4u;   /* coredll CreateProcessW entry */

/* Read up to `cap-1` guest wide chars at `va` into an ASCII-ish buffer for
   logging (non-ASCII -> '?'). Stops at NUL or first unmapped page. */
void ReadGuestWStr(const TraceContext& c, uint32_t va, char* out, int cap) {
    int i = 0;
    if (!va) { out[0] = 0; return; }
    for (; i < cap - 1; ++i) {
        auto ch = c.ReadVa16(va + (uint32_t)i * 2u);
        if (!ch || *ch == 0) break;
        out[i] = (*ch >= 0x20 && *ch < 0x7F) ? (char)*ch : '?';
    }
    out[i] = 0;
}

class TraceOdoT8ExplorerLaunch : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kOdoBundleCrc32, [&tm] {
            tm.OnPc(kCreateProcessW, [](const TraceContext& c) {
                char app[96], cmd[96];
                ReadGuestWStr(c, c.regs[0], app, 96);
                ReadGuestWStr(c, c.regs[1], cmd, 96);
                LOG(Trace, "[T8_CP] CreateProcessW caller_pid=0x%08X "
                           "app='%s' cmd='%s' R2=0x%08X R3=0x%08X LR=0x%08X\n",
                    c.emu.Get<ArmMmu>().State()->process_id,
                    app, cmd, c.regs[2], c.regs[3], c.regs[14]);
            });
        });
    }
};

REGISTER_SERVICE(TraceOdoT8ExplorerLaunch);

}  /* namespace */
