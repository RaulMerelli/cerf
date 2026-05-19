#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "wm5_bundle.h"

namespace {

class TraceWm5AbortVecWatches : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            auto vec_hook = [&tm](uint32_t va) {
                tm.OnPc(va, [va](const TraceContext& c) {
                    auto& mem = c.emu.Get<EmulatedMemory>();
                    const uint32_t l2_entry = mem.ReadWord(0x314A5FC0u);
                    const uint32_t vec_pa_base = l2_entry & 0xFFFFF000u;
                    const uint32_t insn_via_l2 =
                        mem.ReadWord(vec_pa_base | (va & 0xFFFu));
                    const uint32_t handler_pa =
                        vec_pa_base + (va & 0xFFFu) + 0x3D8u + 8u;
                    const uint32_t handler_addr = mem.ReadWord(handler_pa);
                    auto insn_tlb = c.ReadVa32(va);
                    LOG(Trace, "[ABORT_VEC_HIT] vec_PC=0x%08X "
                               "insn(TLB)=0x%08X L2_entry=0x%08X "
                               "vec_PA_base=0x%08X insn(L2)=0x%08X "
                               "handler_PA=0x%08X handler_addr=0x%08X "
                               "caller_LR=0x%08X CPSR=0x%08X\n",
                        va,
                        insn_tlb ? *insn_tlb : 0xDEADBEEFu,
                        l2_entry, vec_pa_base, insn_via_l2,
                        handler_pa, handler_addr, c.regs[14], c.cpsr);
                });
            };
            vec_hook(0xFFFF000Cu);   /* prefetch abort */
            vec_hook(0xFFFF0010u);   /* data abort     */
            vec_hook(0xFFFF0014u);   /* address (reserved) */
            vec_hook(0xFFFF0018u);   /* IRQ */
        });
    }
};

REGISTER_SERVICE(TraceWm5AbortVecWatches);

}  /* namespace */
