#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* atadisk.dll ATAConfig (sub_1F522F8) bail-point hooks. Addresses are raw
   atadisk.dll IDA VAs: XIP exec VA = link VA, NOT the RomParser loadVA. */
class CfAtaDetectProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kPpc2000BundleCrc32, [this] {
            auto& t = emu_.Get<TraceManager>();

            t.OnPc(0x1F528D8u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] DetectATADisk socket=%u funcid=%u\n",
                    c.regs[0] & 0xFFFFu, c.regs[1] & 0xFFu);
            });
            t.OnPc(0x1F522F8u, [](const TraceContext&) {
                LOG(Trace, "[CFATA] ATAConfig enter\n");
            });
            t.OnPc(0x1F52328u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardRequestSocketMask result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0x1F52360u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardGetParsedTuple(27) result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0x1F5250Cu, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] GetATAWindows result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0x1F52528u, [](const TraceContext&) {
                LOG(Trace, "[CFATA] *** return 3: no usable I/O config ***\n");
            });
            t.OnPc(0x1F52660u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] isVoltageNear req=%u nominal=%u\n",
                    c.regs[0] & 0xFFFFu, c.regs[1] & 0xFFFFu);
            });
            t.OnPc(0x1F52418u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] entry NumIOEntries=%u\n", c.regs[3]);
            });
            t.OnPc(0x1F52444u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] IOBase=0x%X wanted=0x%X\n",
                    c.regs[3], c.regs[2]);
            });
            t.OnPc(0x1F52458u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] GetATAWindows(loop) result=0x%X\n",
                    c.regs[0]);
            });
            /* pcmcia.dll MDD (XIP at IDA VA): window grant + map. */
            t.OnPc(0xF24B8Cu, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardRequestWindow h=0x%X parms=0x%X\n",
                    c.regs[0], c.regs[1]);
            });
            t.OnPc(0xF24E50u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardMapWindow hWnd=0x%X base=0x%X len=0x%X\n",
                    c.regs[0], c.regs[1], c.regs[2]);
            });
            t.OnPc(0xF25228u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardMapWindow FAIL err=%u\n", c.regs[5]);
            });
            t.OnPc(0xF24A80u, [](const TraceContext& c) {
                const uint32_t gran_p = c.ReadVa32(0xF294D8u).value_or(0);
                const uint32_t gran =
                    gran_p ? c.ReadVa32(gran_p).value_or(0) : 0;
                LOG(Trace, "[CFATA] FindWindow type=0x%X attrs=0x%X size=0x%X "
                    "r3=0x%X gran=0x%X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3], gran);
                uint32_t r = c.ReadVa32(0xF293E4u).value_or(0);
                for (int i = 0; r && i < 8; ++i) {
                    LOG(Trace, "[CFATA]   region %d @0x%08X used=0x%X "
                        "size=0x%X caps=0x%04X%04X type=0x%X\n",
                        i, r, c.ReadVa32(r + 4u).value_or(0),
                        c.ReadVa32(r + 16u).value_or(0),
                        c.ReadVa32(r + 20u).value_or(0) & 0xFFFFu,
                        (c.ReadVa32(r + 20u).value_or(0) >> 16) & 0xFFFFu,
                        c.ReadVa32(r + 28u).value_or(0) & 0xFFu);
                    r = c.ReadVa32(r).value_or(0);
                }
            });
            t.OnPc(0xF24F18u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] FindWindow result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0x1F5255Cu, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardRequestIRQ result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0x1F525C8u, [](const TraceContext& c) {
                LOG(Trace, "[CFATA] CardRequestConfiguration(COR) result=0x%X\n",
                    c.regs[0]);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(CfAtaDetectProbe);

#endif  /* CERF_DEV_MODE */
