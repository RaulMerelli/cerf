#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* sub_800570A0: J720 kernel banner + register dump; the re-entry count shows
   whether early init is looping. */
class ResetKernelDbgProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            emu_.Get<TraceManager>().OnPc(0x800570A0u, [this](const TraceContext& c) {
                LOG(Trace, "[NKDBG] <<< banner/fault-dump entry #%u r0=0x%08X "
                    "lr=0x%08X >>>\n", ++banner_hits_, c.regs[0], c.regs[14]);
            });
        });
    }

private:
    uint32_t banner_hits_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(ResetKernelDbgProbe);

#endif  /* CERF_DEV_MODE */
