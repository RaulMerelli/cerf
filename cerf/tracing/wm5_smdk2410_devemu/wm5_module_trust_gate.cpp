#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

/* WM5 module-auth regression probe: reads the live trust-policy state the
   cerf_guest_stub LoadLibrary of the RAM-file body trips on. Reads only. */

namespace {

class TraceWm5ModuleTrustGate : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&tm] {
            /* 0x80094D8C: TST R3,#0x10 - R3 = system trust flags; R5 =
               default-trust value; R6 = module descriptor; R8 = trust dest. */
            tm.OnPc(0x80094D8Cu, [](const TraceContext& c) {
                auto flags_mem = c.ReadVa32(0x8148C140u);
                LOG(Trace, "[WM5TRUST] verifier flags R3=0x%08X "
                           "(mem[0x8148C140]=%s) defTrust(R5)=%u descr(R6)=0x%08X "
                           "trustDest(R8)=0x%08X bit0x10(skip)=%u bit0x02=%u\n",
                    c.regs[3],
                    flags_mem ? "ok" : "UNMAPPED",
                    c.regs[5], c.regs[6], c.regs[8],
                    (c.regs[3] & 0x10u) ? 1u : 0u,
                    (c.regs[3] & 0x02u) ? 1u : 0u);
                if (flags_mem)
                    LOG(Trace, "[WM5TRUST]   mem[0x8148C140]=0x%08X\n", *flags_mem);
            });
            /* 0x80094DBC: CMP R0,#0 after file-path sub_8008E8D4 - R0 =
               verify result (0 reject / 1 untrusted / 2 trusted). */
            tm.OnPc(0x80094DBCu, [](const TraceContext& c) {
                LOG(Trace, "[WM5TRUST] FSD verify result R0=%u\n", c.regs[0]);
            });
            /* 0x80094EBC: loc_80094EBC reject (sub_80094CD8 returns 0x80090006). */
            tm.OnPc(0x80094EBCu, [](const TraceContext& c) {
                LOG(Trace, "[WM5TRUST] *** verifier REJECT @0x80094EBC "
                           "(returns 0x80090006) LR=0x%08X\n", c.regs[14]);
            });
            /* 0x8009AFCC: post-verify PROCESS-trust gate. R7=0xFFFFC890,
               R4=module descriptor. Reject iff pCurPrc->[3]==2 && descr+0xCE==1. */
            tm.OnPc(0x8009AFCCu, [](const TraceContext& c) {
                auto pcurprc = c.ReadVa32(0xFFFFC890u);
                uint32_t proc_trust = 0xFFu, mod_trust = 0xFFu;
                if (pcurprc) {
                    auto pt = c.ReadVa8(*pcurprc + 3u);
                    if (pt) proc_trust = *pt;
                }
                auto mt = c.ReadVa8(c.regs[4] + 0xCEu);
                if (mt) mod_trust = *mt;
                LOG(Trace, "[WM5TRUST] gate @0x8009AFCC pCurPrc=0x%08X "
                           "procTrust(+3)=0x%02X modTrust(+0xCE)=0x%02X "
                           "-> %s\n",
                    pcurprc ? *pcurprc : 0u, proc_trust, mod_trust,
                    (proc_trust == 2 && mod_trust == 1) ? "REJECT 0x80090006"
                                                        : "pass");
            });
        });
    }
};

REGISTER_SERVICE(TraceWm5ModuleTrustGate);

}  /* namespace */
