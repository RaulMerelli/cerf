#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "jornada820_bundle.h"

#include <cstdint>

#if CERF_DEV_MODE

namespace {

/* glidepad.dll executes at its link VA (~0x12B0000). Bisect GLD_Init: if the
   entry fires but a later step doesn't, the driver loaded but bailed there;
   if the entry never fires, the driver was never enumerated/loaded. */
constexpr uint32_t kGldInit   = 0x12B248Cu;  /* GLD_Init entry */
constexpr uint32_t kSub1534   = 0x12B1534u;  /* maps a1[0..3] + sub_12B1260 */
constexpr uint32_t kSub1260   = 0x12B1260u;  /* PS/2 controller init */
constexpr uint32_t kSub148C   = 0x12B148Cu;  /* reset handshake */
constexpr uint32_t kSub11D8   = 0x12B11D8u;  /* send byte to controller */
constexpr uint32_t kSub1E58   = 0x12B1E58u;  /* reached after init (mode read) */

void OnceLog(const char* tag, const TraceContext& c) {
    LOG(Trace, "[J820-GLDLOAD] %s pc=0x%08X lr=0x%08X r0=0x%08X\n",
        tag, c.regs[15], c.regs[14], c.regs[0]);
}

class Jornada820GlidepadLoadProbe : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kJornada820BundleCrc32, [&] {
            tm.OnPc(kGldInit, [](const TraceContext& c) {
                static bool once = false; if (once) return; once = true;
                OnceLog("GLD_Init", c);
            });
            tm.OnPc(kSub1534, [](const TraceContext& c) {
                static bool once = false; if (once) return; once = true;
                OnceLog("sub_12B1534(map)", c);
            });
            tm.OnPc(kSub1260, [](const TraceContext& c) {
                static bool once = false; if (once) return; once = true;
                OnceLog("sub_12B1260(ps2-init)", c);
            });
            tm.OnPc(kSub148C, [](const TraceContext& c) {
                static bool once = false; if (once) return; once = true;
                OnceLog("sub_12B148C(reset)", c);
            });
            tm.OnPc(kSub11D8, [](const TraceContext& c) {
                static int n = 0; if (n++ >= 3) return;
                const uint32_t a1   = c.regs[0];
                const uint32_t ctrl = c.ReadVa32(a1 + 8).value_or(0xDEADu);
                auto status = c.ReadVa32(ctrl + 0x400u);
                /* fast-path peek hits RAM (returns a value) but not a peripheral
                   (nullopt) -> tells us whether ctrlVA routes to my device. */
                LOG(Trace, "[J820-GLDLOAD] send: ctrlVA=0x%08X is_RAM=%d status_val=0x%X\n",
                    ctrl, status.has_value() ? 1 : 0, status.value_or(0xFFFFFFFFu));
            });
            tm.OnPc(kSub1E58, [](const TraceContext& c) {
                static bool once = false; if (once) return; once = true;
                OnceLog("sub_12B1E58(mode-read)", c);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Jornada820GlidepadLoadProbe);

#endif  /* CERF_DEV_MODE */
