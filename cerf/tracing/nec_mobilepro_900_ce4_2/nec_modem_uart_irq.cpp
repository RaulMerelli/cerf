#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_cpu.h"
#include "../../jit/cpu_state.h"
#include "bundle.h"

#include <cstdint>

/* Grounds the TL16C550 modem-UART data interrupt -> GPIO: the op==0 enable on
   the OAL edge primitive firing right after the UART IST setup names its GPIO. */
namespace {

uint32_t Cyc(const TraceContext& c) {
    return c.emu.Get<ArmCpu>().State()->guest_cycle_counter;
}

class TraceNecModemUartIrq : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&] {
            /* nk.exe!sub_9023D13C(slot, op): only GRER/GFER setter. slot encodes
               GPIO = (slot>>1)+2, edge = slot&1. op 0=enable 1=disable. */
            tm.OnPc(0x9023D13Cu, [n = uint32_t{0}](const TraceContext& c) mutable {
                const uint32_t slot = c.regs[0];
                const uint32_t op   = c.regs[1];
                if (op == 0u || op == 1u) {
                    if (++n > 200u) return;
                    LOG(Trace, "[gpio-op] #%u op=%s slot=%u GPIO=%u edge=%s "
                               "LR=0x%08X cyc=0x%08X\n",
                        n, op == 0u ? "ENABLE" : "disable", slot,
                        (slot >> 1) + 2u, (slot & 1u) ? "fall" : "rise",
                        c.regs[14], Cyc(c));
                }
            });

            /* tl16c550.dll is an XIP ROM module at its fixed link VA, mapped only
               into device.exe, so these user-VA hooks need no process filter. */
            tm.OnPc(0x01AE24B4u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 8u) return;
                LOG(Trace, "[uart-ist-setup] #%u dev=0x%08X LR=0x%08X cyc=0x%08X\n",
                    n, c.regs[0], c.regs[14], Cyc(c));
            });

            /* UART InterruptInitialize BL: R0 = sysintr, R1 = hEvent. */
            tm.OnPc(0x01AE24D4u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 8u) return;
                LOG(Trace, "[uart-intinit] #%u sysintr=0x%08X hEvent=0x%08X "
                           "cyc=0x%08X\n",
                    n, c.regs[0], c.regs[1], Cyc(c));
            });

            /* ACState (IRQ 38) REQUEST_SYSINTR BL: R2 = IRQ. NOT the UART line. */
            tm.OnPc(0x01AE1CF0u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4u) return;
                LOG(Trace, "[acstate-reqsysintr] #%u ioctl=0x%08X irq=%u "
                           "cyc=0x%08X\n",
                    n, c.regs[0], c.regs[2], Cyc(c));
            });

            /* IIChain install sub_1AE4404 entry (R0 = dev): IRQ = *(dev+0xC0),
               gate first WCHARs at dev+0xCC (DLL) / dev+0x14C (func). The gate
               skips install if IRQ==-1 or either name is empty. */
            tm.OnPc(0x01AE4404u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4u) return;
                const uint32_t dev = c.regs[0];
                LOG(Trace, "[iichain-entry] #%u dev=0x%08X irq=0x%08X "
                           "dll_w0=0x%04X func_w0=0x%04X cyc=0x%08X\n",
                    n, dev, c.ReadVa32(dev + 0xC0u).value_or(0xDEADBEEFu),
                    c.ReadVa16(dev + 0xCCu).value_or(0xFFFFu),
                    c.ReadVa16(dev + 0x14Cu).value_or(0xFFFFu), Cyc(c));
            });

            /* LoadIntChainHandler BL (only reached if the gate passed): R2 = IRQ,
               R0 = DLL-name ptr (dev+0xCC). Firing here proves the modem UART is
               interrupt-driven via a kernel IIChain ISR on this IRQ's GPIO. */
            tm.OnPc(0x01AE45D0u, [n = uint32_t{0}](const TraceContext& c) mutable {
                if (++n > 4u) return;
                const uint32_t dll = c.regs[0];
                char name[24];
                int k = 0;
                for (; k < 23; ++k) {
                    const uint32_t w = c.ReadVa16(dll + 2u * k).value_or(0u);
                    if (w == 0u) break;
                    name[k] = (w >= 0x20u && w < 0x7Fu) ? static_cast<char>(w) : '?';
                }
                name[k] = '\0';
                LOG(Trace, "[iichain-install] #%u irq=%u dll=\"%s\" cyc=0x%08X\n",
                    n, c.regs[2], name, Cyc(c));
            });
        });
    }
};

REGISTER_SERVICE(TraceNecModemUartIrq);

}  /* namespace */
