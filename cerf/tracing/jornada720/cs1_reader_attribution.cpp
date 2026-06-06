#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* Names the driver byte-reading CS1 PA 0x08000048 (boot halts there:
   "no peripheral registered"). Dumps args/caller + instruction bytes at the
   faulting PC so the module can be matched against the extracted ROM. */
class Cs1ReaderAttribution : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kBundleCrc32, [&tm] {
            /* Deliberately UNFILTERED user-VA hook: the owning module is the
               unknown this hook exists to discover (no resolver exists for
               this bundle yet), and the dumped instruction bytes attribute
               each fire by signature no matter which process executes it. */
            tm.OnPc(0x00F199C0u, [](const TraceContext& c) {
                LOG(Trace, "[CS1RD] pc=0x%08X R0=0x%08X R1=0x%08X R2=0x%08X "
                    "R3=0x%08X R4=0x%08X LR=0x%08X SP=0x%08X\n",
                    c.pc, c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[4], c.regs[14], c.regs[13]);
                for (uint32_t off = 0; off < 32; off += 16) {
                    const uint32_t a = c.pc - 16 + off;
                    LOG(Trace, "[CS1RD] code[0x%08X]=%08X %08X %08X %08X\n", a,
                        c.ReadVa32(a).value_or(0xDEADBEEFu),
                        c.ReadVa32(a + 4).value_or(0xDEADBEEFu),
                        c.ReadVa32(a + 8).value_or(0xDEADBEEFu),
                        c.ReadVa32(a + 12).value_or(0xDEADBEEFu));
                }
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Cs1ReaderAttribution);

#endif  /* CERF_DEV_MODE */
