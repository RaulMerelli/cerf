#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* Kernel valloc panic: sub_902166C0 reserves RAM via the allocator sub_9021DB54;
   failing call site 0x902167EC, allocator entry 0x9021DB54. */
namespace {

class TraceNecVallocPanic : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&] {
            /* sub_902166C0 entry: walk MEMORY[0x90295F20] (region list) to see
               whether it is empty or all regions have start==end (which leaves
               v0/v1 at their init values -1/0). node[0]=next node[4]=desc,
               desc[0]=start desc[4]=end desc[16]=count. */
            tm.OnPc(0x902166C0u, [](const TraceContext& c) {
                const uint32_t head = c.ReadVa32(0x90295F20u).value_or(0xDEADBEEFu);
                LOG(Trace, "[reglist] head@0x90295F20=0x%08X\n", head);
                uint32_t node = head;
                for (int i = 0; i < 10 && node && node != 0xDEADBEEFu; ++i) {
                    const uint32_t next  = c.ReadVa32(node + 0).value_or(0xDEADBEEFu);
                    const uint32_t desc  = c.ReadVa32(node + 4).value_or(0xDEADBEEFu);
                    const uint32_t start = c.ReadVa32(desc + 0).value_or(0xDEADBEEFu);
                    const uint32_t end   = c.ReadVa32(desc + 4).value_or(0xDEADBEEFu);
                    const uint32_t cnt   = c.ReadVa32(desc + 16).value_or(0xDEADBEEFu);
                    LOG(Trace, "[reglist] node[%d]@0x%08X next=0x%08X desc=0x%08X "
                               "start=0x%08X end=0x%08X count=%d\n",
                        i, node, next, desc, start, end, cnt);
                    node = next;
                }
            });

            /* The failing reservation BL @ 0x902167EC: R0=base(v11), R1=size
               (v1-v10), R4=v10, R9=v1. Kernel globals: MEMORY[0x90295F24]=v10,
               0x90295DB8 (region floor), 0xFFFFCB28=v10. */
            tm.OnPc(0x902167ECu, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                LOG(Trace, "[valloc-call] #%u base=0x%08X size=0x%08X v10=0x%08X "
                           "v1=0x%08X (size>=32M? %d)\n",
                    n, c.regs[0], c.regs[1], c.regs[4], c.regs[9],
                    c.regs[1] >= 0x2000000u);
            });
            /* After the BL @ 0x902167F0: R0 = return (0 => panic). */
            tm.OnPc(0x902167F0u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                LOG(Trace, "[valloc-ret] #%u R0=0x%08X %s\n",
                    n, c.regs[0], c.regs[0] == 0 ? "FAIL->panic" : "ok");
            });
            /* The first reservation BL @ 0x9021679C-ish runs earlier; hook the
               allocator entry to see every reservation's args. sub_9021DB54
               entry: R0=base R1=size R2=flags R3=type. */
            tm.OnPc(0x9021DB54u, [n = uint32_t{0}](const TraceContext& c) mutable {
                ++n;
                if (n <= 40)
                    LOG(Trace, "[valloc-entry] #%u base=0x%08X size=0x%08X "
                               "flags=0x%08X type=0x%08X LR=0x%08X\n",
                        n, c.regs[0], c.regs[1], c.regs[2], c.regs[3], c.regs[14]);
            });
        });
    }
};

REGISTER_SERVICE(TraceNecVallocPanic);

}  /* namespace */
