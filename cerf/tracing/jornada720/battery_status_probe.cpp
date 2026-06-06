#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* Passive observation of battdrv.dll's battery reads: ACLineStatus (sub_ED18D8),
   main/backup ADC (sub_ED1814/sub_ED1AC8 args), and the SetPPSR present-bit
   (sub_ED1B4C). Regression alarm + map for the battery-model hook sites. */
class BatteryStatusProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            auto& tm = emu_.Get<TraceManager>();

            tm.OnPc(0xED1948u, [this](const TraceContext& c) {
                const uint32_t ac = c.regs[0];
                ++ac_fires_;
                LOG(Trace, "[BATPROBE] ACLineStatus #%u = %u (%s)\n",
                    ac_fires_, ac, ac ? "AC online" : "on battery");
            });

            tm.OnPc(0xED1814u, [this](const TraceContext& c) {
                if (main_logged_) return;
                main_logged_ = true;
                LOG(Trace, "[BATPROBE] main ADC = 0x%03X\n", c.regs[1] & 0x3FF);
            });

            tm.OnPc(0xED1AC8u, [this](const TraceContext& c) {
                if (backup_logged_) return;
                backup_logged_ = true;
                LOG(Trace, "[BATPROBE] backup ADC = 0x%03X\n", c.regs[1] & 0x3FF);
            });

            tm.OnPc(0xED1BB4u, [this](const TraceContext& c) {
                if (present_logged_) return;
                present_logged_ = true;
                LOG(Trace, "[BATPROBE] SetPPSR=0x%08X present-bit(0x200)=%s\n",
                    c.regs[0], (c.regs[0] & 0x200u) ? "ABSENT" : "present");
            });
        });
    }

private:
    uint32_t ac_fires_      = 0;
    bool     main_logged_   = false;
    bool     backup_logged_ = false;
    bool     present_logged_= false;
};

}  /* namespace */

REGISTER_SERVICE(BatteryStatusProbe);

#endif  /* CERF_DEV_MODE */
