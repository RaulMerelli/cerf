#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* TL16C550 modem-UART probes at 0x0B000000. tl16c550.dll runs XIP at its link
   VA, so these PCs are unambiguous (no process filter needed): (1) 0x1AE1930 —
   the 0x55 codec-detect write, register-filtered to fingerprint the module;
   (2) 0x1AE1CE8 — R2 holds the modem IRQ before the OAL SYSINTR request. */
namespace {

class TraceNecCs2WriterId : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&] {
            tm.OnPc(0x01AE1930u, [n = uint32_t{0}](const TraceContext& c) mutable {
                bool is_codec_write = false;
                for (int i = 0; i < 8; ++i)
                    if (c.regs[i] == 0x55u) is_codec_write = true;
                if (!is_codec_write || ++n > 4) return;

                const uint32_t pc = c.pc;
                LOG(Trace, "[cs2-wid] #%u pc=0x%08X LR=0x%08X cpsr=0x%08X "
                           "R0=%08X R1=%08X R2=%08X R3=%08X R4=%08X R5=%08X "
                           "R6=%08X R7=%08X\n",
                    n, pc, c.regs[14], c.cpsr, c.regs[0], c.regs[1], c.regs[2],
                    c.regs[3], c.regs[4], c.regs[5], c.regs[6], c.regs[7]);
                LOG(Trace, "[cs2-wid] #%u bytes @pc-8..+16: %08X %08X %08X "
                           "%08X %08X %08X %08X\n",
                    n,
                    c.ReadVa32(pc - 8).value_or(0xDEADBEEFu),
                    c.ReadVa32(pc - 4).value_or(0xDEADBEEFu),
                    c.ReadVa32(pc + 0).value_or(0xDEADBEEFu),
                    c.ReadVa32(pc + 4).value_or(0xDEADBEEFu),
                    c.ReadVa32(pc + 8).value_or(0xDEADBEEFu),
                    c.ReadVa32(pc + 12).value_or(0xDEADBEEFu),
                    c.ReadVa32(pc + 16).value_or(0xDEADBEEFu));
            });

            tm.OnPc(0x01AE1CE8u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 2) return;
                const uint32_t devinfo =
                    c.ReadVa32(c.regs[4] + 0x254u).value_or(0xDEADBEEFu);
                LOG(Trace, "[modem-irq] #%u IRQ(R2)=0x%08X ctx=0x%08X "
                           "devinfo=0x%08X *(devinfo+4)=0x%08X\n",
                    n, c.regs[2], c.regs[4], devinfo,
                    c.ReadVa32(devinfo + 4u).value_or(0xDEADBEEFu));
            });
        });
    }
};

REGISTER_SERVICE(TraceNecCs2WriterId);

}  /* namespace */
