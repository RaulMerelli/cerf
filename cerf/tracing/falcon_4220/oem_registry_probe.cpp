#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include "bundle.h"

#if CERF_DEV_MODE

namespace {

/* coredll Read/WriteRegistryToOEM (PSL to OAL; runtime VA == IDA VA): read entry
   0x3F8869C R0=flags R1=buf; 0x3F88700 R0=bytes read (filesys treats ==4 as
   "persisted registry present"); write entry 0x3F88718. Unfiltered census; only
   filesys exercises this at boot. */
class FalconOemRegistryProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            tm.OnPc(0x3F8869Cu, [](const TraceContext& c) {
                /* OAL sub_800D30C4 gates: mode = *(*(0xFFFFC890)+3) (must==2),
                   backend fn ptr = *(0x822B7120) (NULL => returns 0). */
                uint32_t mode = 0xFF, backend = 0;
                if (auto k = c.ReadVa32(0xFFFFC890u); k && *k)
                    if (auto m = c.ReadVa8(*k + 3u)) mode = *m;
                if (auto b = c.ReadVa32(0x822B7120u)) backend = *b;
                LOG(Trace, "[OEMREG] ReadRegistryFromOEM flags=0x%X mode=0x%X "
                    "backend=0x%08X\n", c.regs[0], mode, backend);
            });
            tm.OnPc(0x3F88700u, [](const TraceContext& c) {
                LOG(Trace, "[OEMREG]   Read -> %d bytes\n", c.regs[0]);
            });
            tm.OnPc(0x3F88718u, [](const TraceContext& c) {
                LOG(Trace, "[OEMREG] WriteRegistryToOEM flags=0x%X\n", c.regs[0]);
            });
            /* Poll the read-backend ptr (kernel DRAM, globally mapped): does the
               OEM registry-persistence backend EVER get registered, and when? */
            tm.OnRunLoopIter([logged = false](const TraceContext& c) mutable {
                if (logged) return;
                if (auto b = c.ReadVa32(0x822B7120u); b && *b) {
                    LOG(Trace, "[OEMREG] backend@0x822B7120 registered = 0x%08X\n", *b);
                    logged = true;
                }
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconOemRegistryProbe);

#endif  /* CERF_DEV_MODE */
