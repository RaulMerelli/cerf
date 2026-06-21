#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* atadisk.dll ATAConfig (sub_1EB1E4C) bail-point hooks. Addresses are raw
   atadisk.dll IDA VAs: XIP exec VA = link VA, NOT the RomParser loadVA - the
   0x8081xxxx twins are loadVA-relative and fire only if that assumption is
   wrong. */
class CfAtaDetectProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kBundleCrc32, [this] {
            auto& t = emu_.Get<TraceManager>();

            t.OnPc(0x1EB242Cu, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] DetectATADisk socket=%u funcid=%u\n",
                    c.regs[0] & 0xFFFFu, c.regs[1] & 0xFFu);
            });
            t.OnPc(0x1EB1E4Cu, [](const TraceContext&) {
                LOG(Trace, "[CFATA] ATAConfig enter\n");
            });
            t.OnPc(0x1EB1E7Cu, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardRequestSocketMask result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0x1EB1EB4u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardGetParsedTuple(27) result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0x1EB2060u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] GetATAWindows result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0x1EB207Cu, [](const TraceContext&) {
                LOG(Trace, "[CFATA] *** return 3: no usable I/O config ***\n");
            });
            t.OnPc(0x1EB20B0u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardRequestIRQ result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0x1EB2118u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardRequestConfiguration(COR) result=0x%X\n",
                    c.regs[0]);
            });

            /* VA-scheme cross-check (loadVA-relative twins). */
            t.OnPc(0x8081742Cu, [](const TraceContext&) {
                LOG(Trace, "[CFATA] (loadVA-rel) DetectATADisk\n");
            });
            t.OnPc(0x80816E4Cu, [](const TraceContext&) {
                LOG(Trace, "[CFATA] (loadVA-rel) ATAConfig enter\n");
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(CfAtaDetectProbe);

#endif  /* CERF_DEV_MODE */
