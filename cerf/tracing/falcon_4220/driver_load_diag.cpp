#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include "bundle.h"

#if CERF_DEV_MODE

namespace {

void ReadWStr(const TraceContext& c, uint32_t va, char* out, int cap) {
    int n = 0;
    for (; n < cap - 1; ++n) {
        auto w = c.ReadVa16(va + n * 2u);
        if (!w || *w == 0) break;
        out[n] = (*w < 0x80u) ? static_cast<char>(*w) : '?';
    }
    out[n] = 0;
}

/* coredll!LoadDriver (shared XIP, runtime VA == IDA VA): 0x3F87670 entry R0 =
   driver name; 0x3F876D0 (after MOV R0,R4) R0 = result (0 = load failed).
   Unfiltered on purpose - whole-system driver-load census; the name string in
   each fire identifies the load, so the calling process is irrelevant. */
class FalconDriverLoadDiag : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            tm.OnPc(0x3F87670u, [](const TraceContext& c) {
                char name[96]; ReadWStr(c, c.regs[0], name, 96);
                LOG(Trace, "[DRVLOAD] LoadDriver('%s')\n", name);
            });
            tm.OnPc(0x3F876D0u, [](const TraceContext& c) {
                LOG(Trace, "[DRVLOAD]   -> result=0x%08X\n", c.regs[0]);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconDriverLoadDiag);

#endif  /* CERF_DEV_MODE */
