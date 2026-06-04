#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include "bundle.h"

#if CERF_DEV_MODE

namespace {

class FalconDocChipIdProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            /* nk.exe!sub_800F3FFC entry: does the OAL DOC probe run this boot? */
            tm.OnPc(0x800F3FFCu, [](const TraceContext&) {
                LOG(Trace, "[DOC-PROBE] sub_800F3FFC entry (OAL DOC probe runs)\n");
            });
            /* 0x800F41E0 (CMP R0,#0x40): R0 = CHIPID the OAL read at 0xB8301000.
               0x200 = reached the controller; else the access never reached it. */
            tm.OnPc(0x800F41E0u, [](const TraceContext& c) {
                LOG(Trace, "[DOC-PROBE] CHIPID read R0=0x%08X (0x200=G3 detected)\n",
                    c.regs[0]);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconDocChipIdProbe);

#endif  /* CERF_DEV_MODE */
