#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

class TraceWm5PfaTrace : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            /* Prefetch-abort handler exit chain - names which exit path
               NK takes for a fault and what state drives the decision.
               sub_8007B6D8 is the lazy-fill helper; pCurThd[5] gates it. */

            tm.OnPc(0x80077584u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077584 (about to call sub_8007B6D8) "
                           "R0=0x%08X (faulting VA after FCSE) LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x8007B6D8u, [](const TraceContext& c) {
                LOG(Trace, "[L1FILL_TRACE] sub_8007B6D8(VA=0x%08X) called from LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x8007B6F4u, [](const TraceContext& c) {
                LOG(Trace, "[L1FILL_TRACE] PC=0x8007B6F4 (slot-mask check) "
                           "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                           "R4=0x%08X R5=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[4], c.regs[5]);
            });
            tm.OnPc(0x80077590u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077590 (sub_8007B6D8 returned) "
                           "R0(0=fail !0=success)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x80077598u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077598 (FAILURE PATH: POP+go-to-dispatch) "
                           "R0=0x%08X\n", c.regs[0]);
            });
            /* Data-abort version of the dispatcher (called via LR=0x80077708
               in our L1FILL_TRACE log). Mirrors the prefetch-abort hooks
               above so we can see sub_8007B6D8's return value for data
               aborts too. */
            tm.OnPc(0x80077708u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077708 (data-abort sub_8007B6D8 returned) "
                           "R0(0=fail !0=success)=0x%08X\n", c.regs[0]);
            });
            tm.OnPc(0x80077714u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077714 (data-abort FAILURE PATH) "
                           "R0=0x%08X\n", c.regs[0]);
            });
            /* sub_8007B6D8 internal branch decisions - narrows which
               failure path returns 0 for the abort-loop VAs. */
            tm.OnPc(0x8007B7D0u, [](const TraceContext& c) {
                LOG(Trace, "[L1FILL_DECISION] PC=0x8007B7D0 (MOVS R1,R3 before BEQ v5==0) "
                           "R3=0x%08X (=v5 process L1 entry; 0=fail)\n", c.regs[3]);
            });
            tm.OnPc(0x8007B7F0u, [](const TraceContext& c) {
                LOG(Trace, "[L1FILL_DECISION] PC=0x8007B7F0 (CMP R3,#0 before BNE L1-already-set) "
                           "R3=0x%08X (=L1 entry; !=0 means already populated -> fail)\n",
                    c.regs[3]);
            });
            tm.OnPc(0x80077624u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077624 (LDR R2 saved-CPSR from base+0x60) "
                           "R0(thread_base)=0x%08X\n", c.regs[0]);
            });
            tm.OnPc(0x80077634u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077634 (BNE 0x80077668 exit decision) "
                           "R1=0x%08X R2=0x%08X (R1 should be 0x10 USR or 0x1F SYS)\n",
                    c.regs[1], c.regs[2]);
            });
            tm.OnPc(0x80077638u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077638 (PATH 1: USR/SYS exit via MOVS PC,LR) "
                           "R0=0x%08X\n", c.regs[0]);
            });
            tm.OnPc(0x80077668u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077668 (PATH 2: else exit via LDM {R0-PC}) "
                           "R0=0x%08X R2=0x%08X\n", c.regs[0], c.regs[2]);
            });
            tm.OnPc(0x80077664u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x80077664 (MOVS PC,LR ERET) "
                           "LR(USR PC after ERET)=0x%08X\n", c.regs[14]);
            });
            tm.OnPc(0x8007767Cu, [](const TraceContext& c) {
                LOG(Trace, "[PFA_TRACE] PC=0x8007767C (LDM {R0-PC} ERET) "
                           "R0=0x%08X\n", c.regs[0]);
            });
        });
    }
};

REGISTER_SERVICE(TraceWm5PfaTrace);

}  /* namespace */

