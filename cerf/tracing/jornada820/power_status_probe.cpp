#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "jornada820_bundle.h"

#include <cstdint>

#if CERF_DEV_MODE

namespace {

/* coredll GetSystemPowerStatusEx (J820 coredll, IDA 0x1FAB634). r0 = the
   caller's reused result buffer, so at call entry it still holds the previous
   poll's filled struct (CE2 SYSTEM_POWER_STATUS_EX: +0 AC, +1 Flag, +2 Pct,
   +12 BackupFlag, +13 BackupPct). Regression alarm for the battery gauge. */
constexpr uint32_t kGetSysPwr = 0x1FAB634u;

class Jornada820PowerStatusProbe : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kJornada820BundleCrc32, [&] {
            tm.OnPc(kGetSysPwr, [this](const TraceContext& c) {
                const uint32_t s = c.regs[0];
                const auto a = c.ReadVa32(s);
                const auto b = c.ReadVa32(s + 12u);
                LOG(Trace, "[J820PWR] AC=%u Flag=0x%02X Pct=%u | "
                    "BkFlag=0x%02X BkPct=%u\n",
                    a.value_or(0xFFu) & 0xFFu,
                    (a.value_or(0) >> 8) & 0xFFu,
                    (a.value_or(0) >> 16) & 0xFFu,
                    b.value_or(0) & 0xFFu,
                    (b.value_or(0) >> 8) & 0xFFu);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Jornada820PowerStatusProbe);

#endif  /* CERF_DEV_MODE */
