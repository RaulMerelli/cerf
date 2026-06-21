#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "wm5_bundle.h"

#include <atomic>

namespace {

class TraceWm5UartBadWrite : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            tm.OnPc(0x03DD239Cu, [this](const TraceContext& c) {
                if (logged_.exchange(true)) return;
                LOG(Trace, "[UART_BAD] hit PC=0x%08X CPSR=0x%08X\n",
                    c.pc, c.cpsr);
                auto read_va = [&](uint32_t va) -> std::optional<uint32_t> {
                    return c.ReadVa32(va);
                };
                /* Nine words centred on PC = 4 before + insn + 4 after. */
                for (int i = -4; i <= 4; ++i) {
                    const uint32_t va = c.pc + (uint32_t)(i * 4);
                    const auto w = read_va(va);
                    if (w) {
                        LOG(Trace, "[UART_BAD]   0x%08X: 0x%08X\n", va, *w);
                    } else {
                        LOG(Trace, "[UART_BAD]   0x%08X: <unmapped>\n", va);
                    }
                }
                /* DLL header words at 0x03DD0000 - for CE5 ROM-DLLs
                   the first 64 bytes is the abbreviated TOC; we just
                   want the bytes for offline binary identification. */
                for (uint32_t off = 0; off < 0x40u; off += 4u) {
                    const auto w = read_va(0x03DD0000u + off);
                    if (w) {
                        LOG(Trace, "[UART_BAD]   DLL@+0x%02X: 0x%08X\n",
                            off, *w);
                    } else {
                        LOG(Trace, "[UART_BAD]   DLL@+0x%02X: <unmapped>\n", off);
                    }
                }
                /* Also dump caller's instruction at LR-4 in slot 0
                   (which mirrors the loaded DLL in the current
                   process). */
                const uint32_t lr = c.regs[14];
                for (int i = -2; i <= 2; ++i) {
                    const uint32_t va = lr + (uint32_t)(i * 4);
                    const auto w = read_va(va);
                    if (w) {
                        LOG(Trace, "[UART_BAD]   LR%+d: 0x%08X: 0x%08X\n",
                            i * 4, va, *w);
                    } else {
                        LOG(Trace, "[UART_BAD]   LR%+d: 0x%08X: <unmapped>\n",
                            i * 4, va);
                    }
                }
                LOG(Trace, "[UART_BAD]   R0=0x%08X R3+R5=0x%08X "
                           "R4+R5=0x%08X R11+R5=0x%08X\n",
                    c.regs[0],
                    c.regs[3] + c.regs[5],
                    c.regs[4] + c.regs[5],
                    c.regs[11] + c.regs[5]);
                /* Read the value at each candidate VA via the
                   data-TLB fast-path peek. */
                auto peek = [&](const char* nm, uint32_t va) {
                    auto v = c.ReadVa32(va);
                    if (v) LOG(Trace, "[UART_BAD]   %s VA=0x%08X value=0x%08X\n",
                               nm, va, *v);
                    else   LOG(Trace, "[UART_BAD]   %s VA=0x%08X value=<unmapped>\n",
                               nm, va);
                };
                peek("R3      ", c.regs[3]);
                peek("R3+R5   ", c.regs[3] + c.regs[5]);
                peek("R0      ", c.regs[0]);
                peek("R4      ", c.regs[4]);
                peek("R4+R5   ", c.regs[4] + c.regs[5]);
            });
        });
    }

private:
    std::atomic<bool> logged_{false};
};

REGISTER_SERVICE(TraceWm5UartBadWrite);

}  /* namespace */
