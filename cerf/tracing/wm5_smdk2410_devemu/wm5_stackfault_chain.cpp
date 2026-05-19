#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

class TraceWm5StackfaultChain : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {

            tm.OnPc(0x8007B8A4u, [](const TraceContext& c) {
                LOG(Trace, "[SF_DISPATCH] sub_8007B8A4 entry ctx=R0=0x%08X "
                           "SP=0x%08X LR=0x%08X CPSR=0x%08X pCurThd=0x%08X "
                           "pCurPrc=0x%08X\n",
                    c.regs[0], c.regs[13], c.regs[14], c.cpsr,
                    c.ReadVa32(0xFFFFC894u).value_or(0xDEADBEEFu),
                    c.ReadVa32(0xFFFFC890u).value_or(0xDEADBEEFu));
            });

            tm.OnPc(0x8007BB30u, [](const TraceContext& c) {
                LOG(Trace, "[SF_BL_PPF] PC=0x8007BB30 about to BL sub_80092364 "
                           "R0(flag)=0x%08X R1=R10=0x%08X SP=0x%08X "
                           "CPSR=0x%08X\n",
                    c.regs[0], c.regs[10], c.regs[13], c.cpsr);
            });

            tm.OnPc(0x80092364u, [](const TraceContext& c) {
                LOG(Trace, "[SF_PPF] sub_80092364 entry R0(flag)=0x%08X "
                           "R1(BVA)=0x%08X SP=0x%08X LR=0x%08X CPSR=0x%08X "
                           "pCurThd=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[13], c.regs[14], c.cpsr,
                    c.ReadVa32(0xFFFFC894u).value_or(0xDEADBEEFu));
            });

            tm.OnPc(0x80092368u, [](const TraceContext& c) {
                LOG(Trace, "[SF_PPF_PUSHED] sub_80092364+4 SP=0x%08X "
                           "(after PUSH {R4-R11,LR})\n", c.regs[13]);
            });

            tm.OnPc(0x80092504u, [](const TraceContext& c) {
                const auto l1_54 = c.ReadVa32(0xFFFD0150u);
                LOG(Trace, "[SF_HND_CALL] handler=R3=0x%08X "
                           "R0(flag)=0x%08X R1(BVA)=0x%08X "
                           "L1[0x54]_pre=%s 0x%08X\n",
                    c.regs[3], c.regs[0], c.regs[1],
                    l1_54 ? "OK" : "FAULT",
                    l1_54.value_or(0xDEADBEEFu));
            });

            tm.OnPc(0x8009250Cu, [](const TraceContext& c) {
                const auto l1_54 = c.ReadVa32(0xFFFD0150u);
                LOG(Trace, "[SF_HND_RET] handler returned R0=0x%08X "
                           "L1[0x54]_post=%s 0x%08X\n",
                    c.regs[0],
                    l1_54 ? "OK" : "FAULT",
                    l1_54.value_or(0xDEADBEEFu));
            });

            tm.OnPc(0x8007BB34u, [](const TraceContext& c) {
                const auto v_142c = c.ReadVa32(0x0142C000u);
                const auto v_542c = c.ReadVa32(0x0542C000u);
                LOG(Trace, "[SF_PPF_RET] sub_80092364 returned R0=0x%08X "
                           "SP=0x%08X CPSR=0x%08X "
                           "peek[0x0142C000]=%s 0x%08X "
                           "peek[0x0542C000]=%s 0x%08X\n",
                    c.regs[0], c.regs[13], c.cpsr,
                    v_142c ? "OK"   : "FAULT",
                    v_142c.value_or(0xDEADBEEFu),
                    v_542c ? "OK"   : "FAULT",
                    v_542c.value_or(0xDEADBEEFu));
            });

            tm.OnPc(0x0142BFF4u, [](const TraceContext& c) {
                LOG(Trace, "[SF_USR_PC] worker at user PC=0x0142BFF4 "
                           "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                           "R10=0x%08X R12=0x%08X SP=0x%08X LR=0x%08X "
                           "CPSR=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[10], c.regs[12], c.regs[13], c.regs[14],
                    c.cpsr);
            });
        });
    }
};

REGISTER_SERVICE(TraceWm5StackfaultChain);

}  /* namespace */
