#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>
#include <string>

/* nk.exe sub_90205054 is the OEM debug char-sink (OEMWriteDebugString level),
   nulled by the NEC OAL (= nullsub_3), so all kernel/device.exe/gwes debug output
   is discarded. Hook it (R0 = char), reassemble lines, re-emit as [NKDBG]. */
namespace {

class TraceNecKernelDbgOutput : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&] {
            tm.OnPc(0x90205054u, [line = std::string{}](const TraceContext& c) mutable {
                const char ch = static_cast<char>(c.regs[0] & 0xFFu);
                if (ch == '\n') {
                    LOG(Trace, "[NKDBG] %s\n", line.c_str());
                    line.clear();
                } else if (ch == '\r') {
                    /* CE emits CRLF; flush on LF, drop CR. */
                } else {
                    line.push_back((ch >= 0x20 && ch < 0x7F) ? ch : '?');
                    if (line.size() >= 512u) {
                        LOG(Trace, "[NKDBG] %s\n", line.c_str());
                        line.clear();
                    }
                }
            });
        });
    }
};

REGISTER_SERVICE(TraceNecKernelDbgOutput);

}  /* namespace */
