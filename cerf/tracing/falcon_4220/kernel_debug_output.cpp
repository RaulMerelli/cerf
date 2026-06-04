#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

namespace {

/* Hook the ENTRY of nk.exe OEMWriteDebugString sub_800E7770 (R0 = wide string):
   this OAL wires no writer into its output sink (0x82296020) and discards every
   string inside the function, so a hook on the sink would capture nothing. */
class Falcon4220KernelDebugOutput : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            tm.OnPc(0x800E7770u, [](const TraceContext& c) {
                char s[256];
                int n = 0;
                for (; n < static_cast<int>(sizeof(s)) - 1; ++n) {
                    auto wc = c.ReadVa16(c.regs[0] + 2u * static_cast<uint32_t>(n));
                    if (!wc || *wc == 0) break;
                    s[n] = (*wc >= 0x20u && *wc < 0x7Fu)
                               ? static_cast<char>(*wc) : '?';
                }
                s[n] = 0;
                LOG(Trace, "[NKDBG] %s\n", s);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Falcon4220KernelDebugOutput);

#endif  /* CERF_DEV_MODE */
