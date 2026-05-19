#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

#include <atomic>

namespace {

/* DisableThreadLibraryCalls R3-resolution chain in coredll at PCs
   0x03F77D14..0x03F77D70. Each hook captures R3/R12/LR/SP/CPSR. */

#define DTLC_HOOK(pc_, tag_)                                                  \
    tm.OnPc(pc_, [](const TraceContext& c) {                                  \
        LOG(Trace, "[" tag_ "] R3=0x%08X R12=0x%08X LR=0x%08X SP=0x%08X "     \
                   "CPSR=0x%08X\n",                                           \
            c.regs[3], c.regs[12], c.regs[14], c.regs[13], c.cpsr);           \
    })

class TraceWm5DtlcChain : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            DTLC_HOOK(0x03F77D14u, "DTLC_ENTRY");
            DTLC_HOOK(0x03F77D1Cu, "DTLC_R3_FFFFC800");
            DTLC_HOOK(0x03F77D24u, "DTLC_R3_LDR1");
            DTLC_HOOK(0x03F77D28u, "DTLC_R3_LDR_M14");
            DTLC_HOOK(0x03F77D2Cu, "DTLC_R3_TST");
            DTLC_HOOK(0x03F77D30u, "DTLC_R3_LDRNE_LIT");
            DTLC_HOOK(0x03F77D34u, "DTLC_R3_LDRNE_DEREF");
            DTLC_HOOK(0x03F77D38u, "DTLC_R3_CMPNE");
            DTLC_HOOK(0x03F77D3Cu, "DTLC_R3_LDRNE_OFF248");
            DTLC_HOOK(0x03F77D40u, "DTLC_R3_LDREQ_PSL");
            DTLC_HOOK(0x03F77D44u, "DTLC_PRE_MOVLR");
            DTLC_HOOK(0x03F77D48u, "DTLC_PRE_BX");
            DTLC_HOOK(0x03F77D4Cu, "DTLC_RETURN");

            tm.OnPc(0x03F5842Cu, [this](const TraceContext& c) {
                int n = probe_count_.fetch_add(1, std::memory_order_relaxed);
                if (n >= 3) return;
                auto lpvTls   = c.ReadVa32(0xFFFFC800u);
                auto flag_w   = lpvTls ? c.ReadVa32(*lpvTls - 0x14u) : std::nullopt;
                auto tbl_s0   = c.ReadVa32(0x01FFF9E4u);
                auto tbl_fcse = c.ReadVa32(0x05FFF9E4u);
                uint32_t tbl = (tbl_fcse && *tbl_fcse) ? *tbl_fcse
                              : (tbl_s0  ? *tbl_s0     : 0);
                auto slot_248 = (tbl != 0) ? c.ReadVa32(tbl + 0x248u)
                                           : std::nullopt;
                auto lit_pdsb = c.ReadVa32(0x03F77D68u);
                auto lit_9e4  = c.ReadVa32(0x03F77D6Cu);
                LOG(Trace, "[DTLC_PROBE n=%d] lpvTls=%X "
                           "*(lpvTls-0x14)=%X "
                           "MEMORY[0x01FFF9E4](s0)=%X "
                           "MEMORY[0x05FFF9E4](fcse)=%X "
                           "MEMORY[table+0x248]=%X "
                           "lit[0x3F77D68](expect 0xF000FDB8)=%X "
                           "lit[0x3F77D6C](expect 0x01FFF9E4)=%X\n",
                    n + 1,
                    lpvTls   ? *lpvTls   : 0xDEADBEEFu,
                    flag_w   ? *flag_w   : 0xDEADBEEFu,
                    tbl_s0   ? *tbl_s0   : 0xDEADBEEFu,
                    tbl_fcse ? *tbl_fcse : 0xDEADBEEFu,
                    slot_248 ? *slot_248 : 0xDEADBEEFu,
                    lit_pdsb ? *lit_pdsb : 0xDEADBEEFu,
                    lit_9e4  ? *lit_9e4  : 0xDEADBEEFu);
            });
        });
    }

private:
    std::atomic<int> probe_count_{0};
};

#undef DTLC_HOOK

REGISTER_SERVICE(TraceWm5DtlcChain);

}  /* namespace */
