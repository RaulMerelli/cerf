#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

/* filesys MountSystemHive - at PC=0x26C88 sub_194AC("Flags") returns;
   at PC=0x26C94 sub_194AC("Start DevMgr") returns. dword_48058 is what
   RegQueryValueExW just wrote. */

class TraceWm5FilesysMsh : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            tm.OnPc(0x26C88u, [](const TraceContext& c) {
                LogFlagsAt(c, "FLAGS_RDY");
            });
            tm.OnPc(0x26C94u, [](const TraceContext& c) {
                LogFlagsAt(c, "SDM_RDY");
            });
        });
    }

private:
    static void LogFlagsAt(const TraceContext& c, const char* tag) {
        const auto flags = c.ReadVa32(0x48058u);
        if (flags) {
            LOG(Trace, "[%s] R0_status=0x%08X dword_48058=0x%08X "
                       "R1=0x%08X R2=0x%08X R3=0x%08X SP=0x%08X\n",
                tag, c.regs[0], *flags,
                c.regs[1], c.regs[2], c.regs[3], c.regs[13]);
        } else {
            LOG(Trace, "[%s] R0_status=0x%08X dword_48058=(unmapped) "
                       "R1=0x%08X R2=0x%08X R3=0x%08X SP=0x%08X\n",
                tag, c.regs[0],
                c.regs[1], c.regs[2], c.regs[3], c.regs[13]);
        }
    }
};

REGISTER_SERVICE(TraceWm5FilesysMsh);

}  /* namespace */
