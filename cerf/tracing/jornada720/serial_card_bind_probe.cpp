#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* serial.dll bind-path hooks (raw IDA VAs = XIP exec VAs). Confirms whether the
   PCMCIA Detect chain reaches the modem detector, matches our CIS, and creates
   the COM device when the SerialPcCard is inserted. */
class SerialCardBindProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kBundleCrc32, [this] {
            auto& t = emu_.Get<TraceManager>();

            t.OnPc(0xF95F98u, [](const TraceContext& c) {
                LOG(Trace, "[SERCARD] DetectModem socket=%u funcid=%u\n",
                    c.regs[0] & 0xFFFFu, c.regs[1] & 0xFFu);
            });
            t.OnPc(0xF95E08u, [](const TraceContext&) {
                LOG(Trace, "[SERCARD] FindComConfig (CardGetParsedTuple CFTABLE)\n");
            });
            t.OnPc(0xF943DCu, [](const TraceContext&) {
                LOG(Trace, "[SERCARD] *** COM_Init: serial.dll creating COM device ***\n");
            });
            t.OnPc(0xF9465Cu, [](const TraceContext&) {
                LOG(Trace, "[SERCARD] COM_Open\n");
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(SerialCardBindProbe);

#endif  /* CERF_DEV_MODE */
