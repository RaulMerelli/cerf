#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"
#include "bundle.h"

#include <memory>
#include <string>
#include <unordered_set>

#if CERF_DEV_MODE

namespace {

/* Unfiltered ON PURPOSE: no name->pid map exists for this bundle yet, and
   identifying which pid runs RunApps is this trace's goal - a pid filter would
   hide that process. Each handler logs the FCSE process_id for attribution. */
class SimpadSl4RunAppsLaunchTrace : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            tm.OnPc(0x16EC0u, [](const TraceContext& c) {          /* filesys RunApps sub_16EC0 */
                LOG(Trace, "[RUNAPPS] entry pid=0x%08X lr=0x%08X\n",
                    Pid(c), c.regs[14]);
            });
            tm.OnPc(0x3A718u, [](const TraceContext& c) {          /* filesys CreateProcessW thunk */
                std::string app = ReadWide(c, c.regs[0]);          /* R0 = lpApplicationName */
                if (app.empty()) app = ReadWide(c, c.regs[1]);     /* else lpCommandLine */
                LOG(Trace, "[RUNAPPS] CreateProcessW pid=0x%08X name=\"%s\" "
                           "r0=0x%08X r1=0x%08X lr=0x%08X\n",
                    Pid(c), app.c_str(), c.regs[0], c.regs[1], c.regs[14]);
            });
            tm.OnPc(0x3A70Cu, [](const TraceContext& c) {          /* filesys WaitForMultipleObjects thunk */
                LOG(Trace, "[RUNAPPS] WaitForMultipleObjects pid=0x%08X "
                           "count=%u handles=0x%08X waitall=%u ms=0x%08X "
                           "lr=0x%08X\n",
                    Pid(c), c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[14]);
            });
            auto seen = std::make_shared<std::unordered_set<uint32_t>>();
            tm.OnPc(0x13824u, [seen](const TraceContext& c) {      /* device.exe start export */
                const uint32_t pid = Pid(c);
                if (!seen->insert(0x13824u ^ pid).second) return;
                LOG(Trace, "[RUNAPPS] device.exe entry reached pid=0x%08X\n", pid);
            });
            tm.OnPc(0x15848u, [seen](const TraceContext& c) {      /* gwes.exe start export */
                const uint32_t pid = Pid(c);
                if (!seen->insert(0x15848u ^ pid).second) return;
                LOG(Trace, "[RUNAPPS] gwes.exe entry reached pid=0x%08X\n", pid);
            });
        });
    }

private:
    static uint32_t Pid(const TraceContext& c) {
        return c.emu.Get<ArmMmu>().State()->process_id;
    }
    static std::string ReadWide(const TraceContext& c, uint32_t base) {
        std::string s;
        if (!base) return s;
        for (uint32_t i = 0; i < 260u; ++i) {
            auto w = c.ReadVa16(base + 2u * i);
            if (!w || *w == 0) break;
            if (*w >= 0x20 && *w < 0x7F) s.push_back(char(*w));
        }
        return s;
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4RunAppsLaunchTrace);

#endif  // CERF_DEV_MODE
