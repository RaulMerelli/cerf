#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>
#include <string>

/* hpc2000 (CE3) OEM debug byte-sink j_nullsub_6 (0x840940F0) is nulled, so the
   kernel banner / OEMInit / exception dumps are discarded. The OAL string
   printer sub_840940C4 calls it per char (R0 = char). Reassemble + re-emit. */
namespace {

class TraceNecHpc2000KernelDbgOutput : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Hpc2000BundleCrc32, [&] {
            tm.OnPc(0x840940F0u, [line = std::string{}](const TraceContext& c) mutable {
                const char ch = static_cast<char>(c.regs[0] & 0xFFu);
                if (ch == '\n') {
                    LOG(Trace, "[NKDBG] %s\n", line.c_str());
                    line.clear();
                } else if (ch == '\r') {
                    /* CRLF: flush on LF, drop CR. */
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

REGISTER_SERVICE(TraceNecHpc2000KernelDbgOutput);

}  /* namespace */
