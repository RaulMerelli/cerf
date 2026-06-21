#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

class TraceWm5KernelLoaderGrid : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            auto k_hook = [&tm](uint32_t pc, const char* tag) {
                tm.OnPc(pc, [tag, pc](const TraceContext& c) {
                    LOG(Trace, "[%s] kernel 0x%08X entered "
                               "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                               "LR=0x%08X SP=0x%08X\n",
                        tag, pc,
                        c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                        c.regs[14], c.regs[13]);
                });
            };
            /* loader region 0x80097..0x80099 */
            k_hook(0x80097138u, "K_97138");
            k_hook(0x80097204u, "K_97204");
            k_hook(0x80097378u, "K_97378");
            k_hook(0x8009760Cu, "K_9760C");
            k_hook(0x800976D0u, "K_PAGEIN");
            k_hook(0x80097910u, "K_97910");
            k_hook(0x80097B40u, "K_97B40");
            k_hook(0x80097DE8u, "K_97DE8");
            k_hook(0x80097EE4u, "K_97EE4");
            k_hook(0x80098158u, "K_98158");
            k_hook(0x800982A8u, "K_982A8");
            k_hook(0x80098360u, "K_98360");
            k_hook(0x800985DCu, "K_985DC");
            k_hook(0x80098660u, "K_LOADO32");
            k_hook(0x800989E4u, "K_989E4");
            k_hook(0x80098AACu, "K_98AAC");
            k_hook(0x80098B1Cu, "K_98B1C");
            k_hook(0x80098C3Cu, "K_98C3C");
            k_hook(0x80098D30u, "K_98D30");
            k_hook(0x80099400u, "K_99400");
            k_hook(0x800997F0u, "K_997F0");
            k_hook(0x80099A1Cu, "K_99A1C");
            k_hook(0x80099CD0u, "K_99CD0");
            k_hook(0x80099EBCu, "K_99EBC");
            /* virt-mem / page-table region 0x80090..0x80094 */
            k_hook(0x80090F74u, "K_VMPAGE");
            k_hook(0x800912C0u, "K_912C0");
            k_hook(0x80091998u, "K_91998");
            k_hook(0x80091BB4u, "K_91BB4");
            k_hook(0x80092AB0u, "K_92AB0");
            k_hook(0x8009403Cu, "K_9403C");
            k_hook(0x80094CD8u, "K_94CD8");
            k_hook(0x80095858u, "K_95858");
            k_hook(0x80095D60u, "K_95D60");
            k_hook(0x80096204u, "K_96204");
            k_hook(0x80096BB8u, "K_96BB8");

            /* sub_80096BB8 import-resolver - STR R8, [R6] at PC=0x80096F2C
               writes resolved IAT value. R8=v27 = import target; R6=
               slot addr. */
            tm.OnPc(0x80096F2Cu, [](const TraceContext& c) {
                LOG(Trace, "[IMP_RESOLVE] STR R8,[R6] at 0x80096F2C "
                           "IAT_slot=R6=0x%08X resolved_value=R8=0x%08X "
                           "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X R4=0x%08X "
                           "R5=0x%08X R7=0x%08X R9=0x%08X R10=0x%08X LR=0x%08X\n",
                    c.regs[6], c.regs[8],
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3], c.regs[4],
                    c.regs[5], c.regs[7], c.regs[9], c.regs[10], c.regs[14]);
            });
        });
    }
};

REGISTER_SERVICE(TraceWm5KernelLoaderGrid);

}  /* namespace */

