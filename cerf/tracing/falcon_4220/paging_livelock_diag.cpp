#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_cpu.h"
#include "../../jit/arm_mmu.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "bundle.h"

#include <atomic>
#include <cstdio>
#include <optional>

#if CERF_DEV_MODE

namespace {

/* Demand-pager sub_800C2768: the HW L1 table is self-mapped at 0xFFFD0000, so
   the fault's L1 entry is 0xFFFD0000 + 4*((faultVA>>20)&0xFFC). present=1 (that
   entry already non-zero) means the pager no-ops (`if (*l1) return 0;`) = CERF
   faulted a VA the guest had already mapped - the CERF translate/TLB divergence. */
class Falcon4220PagingLivelockDiag : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            const TracePredicate in_gwes = [](const TraceContext& c) {
                return (c.emu.Get<ArmMmu>().State()->process_id >> 25) == 5u;
            };
            tm.OnPc(0x800C2768u, [](const TraceContext& c) {
                static std::atomic<uint64_t> n{0};
                const uint64_t k = n.fetch_add(1, std::memory_order_relaxed);
                if (k >= 300u && (k & 0x1FFu) != 0u) return;
                const uint32_t va     = c.regs[0];
                const uint32_t l1addr = 0xFFFD0000u + 4u * ((va >> 20) & 0xFFCu);
                const uint32_t l1     = c.ReadVa32(l1addr).value_or(0xDEADBEEFu);
                const uint32_t cur    = c.ReadVa32(0xFFFFC894u).value_or(0u);
                const uint32_t aky    = cur ? c.ReadVa32(cur + 20u).value_or(0u) : 0u;
                LOG(Trace, "[PGLL] pager n=%llu faultVA=0x%08X l1@0x%08X=0x%08X "
                           "present=%d slot=%u curId=0x%08X aky=0x%08X\n",
                    static_cast<unsigned long long>(k), va, l1addr, l1,
                    (l1 != 0u && l1 != 0xDEADBEEFu) ? 1 : 0, va >> 25,
                    c.ReadVa32(0xFFFFC808u).value_or(0u), aky);
            });
            tm.OnPc(0x800C1E00u, [](const TraceContext& c) {
                static std::atomic<uint64_t> n{0};
                const uint64_t k = n.fetch_add(1, std::memory_order_relaxed);
                if (k >= 300u && (k & 0x1FFu) != 0u) return;
                LOG(Trace, "[PGLL] flush n=%llu slotBaseVA=0x%08X slot=%u dirty=0x%08X\n",
                    static_cast<unsigned long long>(k), c.regs[0], c.regs[0] >> 25,
                    c.ReadVa32(0x822B94C0u).value_or(0xDEADBEEFu));
            });

            /* sub_1C6C4 = gwes FindWindow-by-name (walks off_B9CA8 under the GWES
               CS, wcsicmp per entry); a1 = the searched wide name. The PSL keeps
               the caller's thread, so curId names the polling thread/process even
               though pc is in gwes. Capture the searched name(s) being polled. */
            tm.OnPcFiltered(0x1C6C4u, in_gwes, [](const TraceContext& c) {
                char nm[64]; int i = 0;
                for (; i < 63; ++i) {
                    auto w = c.ReadVa16(c.regs[0] + 2u * i);
                    if (!w || *w == 0) break;
                    nm[i] = (*w < 0x80u) ? static_cast<char>(*w) : '?';
                }
                nm[i] = 0;
                static std::atomic<uint64_t> n{0};
                const uint64_t k = n.fetch_add(1, std::memory_order_relaxed);
                if (k < 60u || (k & 0x3FFu) == 0u)
                    LOG(Trace, "[PGLL] findwnd n=%llu name='%s' a1=0x%08X curId=0x%08X\n",
                        static_cast<unsigned long long>(k), nm, c.regs[0],
                        c.ReadVa32(0xFFFFC808u).value_or(0u));
            });

            /* One-shot dump of the spin loop's instruction bytes (device.exe
               slot 3) so the DLL is matched in the extracted ROM by .text. */
            const TracePredicate in_dev = [](const TraceContext& c) {
                return (c.emu.Get<ArmMmu>().State()->process_id >> 25) == 3u;
            };
            tm.OnPcFiltered(0x017F37FCu, in_dev, [](const TraceContext& c) {
                static std::atomic<bool> done{false};
                if (done.exchange(true)) return;
                char buf[400]; int n = 0;
                for (uint32_t va = 0x017F1D28u; va <= 0x017F1D68u && n < 180; va += 4)
                    n += std::snprintf(buf + n, sizeof(buf) - n, "%08X ",
                                       c.ReadVa32(va).value_or(0xDEADBEEFu));
                n += std::snprintf(buf + n, sizeof(buf) - n, "| @3784: ");
                for (uint32_t va = 0x017F3784u; va <= 0x017F37C4u && n < 380; va += 4)
                    n += std::snprintf(buf + n, sizeof(buf) - n, "%08X ",
                                       c.ReadVa32(va).value_or(0xDEADBEEFu));
                LOG(Trace, "[SPIN-BYTES] %s\n", buf);
            });

            /* Per-window distinct (curId, pc) sampled every 64th Run() and reset
               per window - names the thread+code spinning during the freeze. */
            tm.OnRunLoopIter([](const TraceContext& c) {
                static std::atomic<uint64_t> beat{0};
                const uint64_t b = beat.fetch_add(1, std::memory_order_relaxed);
                if ((b & 0x3Fu) != 0u) return;
                static std::atomic<uint32_t> samp{0};
                const uint32_t s = samp.fetch_add(1, std::memory_order_relaxed);
                static std::atomic<uint32_t> seen[64];
                static std::atomic<uint32_t> seen_n{0};
                if ((s & 0x1FFFu) == 0u) {
                    seen_n.store(0, std::memory_order_relaxed);
                    LOG(Trace, "[SPIN-WS] === window %u ===\n", s >> 13);
                }
                const uint32_t pc  = c.pc;
                const uint32_t cur = c.ReadVa32(0xFFFFC808u).value_or(0u);
                const uint32_t key = pc ^ (cur * 2654435761u);
                const uint32_t have = seen_n.load(std::memory_order_relaxed);
                for (uint32_t i = 0; i < have && i < 64u; ++i)
                    if (seen[i].load(std::memory_order_relaxed) == key) return;
                if (have < 64u) {
                    seen[have].store(key, std::memory_order_relaxed);
                    seen_n.store(have + 1u, std::memory_order_relaxed);
                }
                LOG(Trace, "[SPIN-WS] pc=0x%08X curId=0x%08X\n", pc, cur);
            });

            /* OST state poll (PXA255 base 0x40A00000 via the dispatcher's OST
               FastRead). When the tick dies, now (0x822B90B4) freezes; this shows
               WHY the match stops: OSCR vs OSMR0 (reached?), OSSR bit0 (stuck
               set?), OIER bit0 (disarmed?). Logged on now-change or every 16384. */
            tm.OnRunLoopIter([](const TraceContext& c) {
                auto now = c.ReadVa32(0x822B90B4u);
                if (!now) return;
                static std::atomic<uint32_t> lastNow{0xFFFFFFFFu};
                static std::atomic<uint64_t> beat{0};
                const uint64_t b = beat.fetch_add(1, std::memory_order_relaxed);
                if (*now == lastNow.exchange(*now, std::memory_order_relaxed) &&
                        (b & 0x3FFFu) != 0u)
                    return;
                auto& d = c.emu.Get<PeripheralDispatcher>();
                LOG(Trace, "[PGLL] now=%u OSCR=0x%08X OSMR0=0x%08X OSSR=0x%X OIER=0x%X\n",
                    *now, d.ReadWord(0x40A00010u), d.ReadWord(0x40A00000u),
                    d.ReadWord(0x40A00014u), d.ReadWord(0x40A0001Cu));
            });

            /* Host-side, non-perturbing: is irq_interrupt_pending stuck, which makes
               WfiHelper early-return and freezes the icount OSCR / kills the OST tick? */
            tm.OnRunLoopIter([](const TraceContext& c) {
                static std::atomic<uint64_t> beat{0};
                const uint64_t b = beat.fetch_add(1, std::memory_order_relaxed);
                if ((b & 0x3Fu) != 0u) return;
                const uint32_t irqp = c.emu.Get<ArmCpu>().State()->irq_interrupt_pending;
                auto& d = c.emu.Get<PeripheralDispatcher>();
                const uint32_t icip = d.ReadWord(0x40D00000u);
                const uint32_t icmr = d.ReadWord(0x40D00004u);
                const uint32_t icpr = d.ReadWord(0x40D00010u);
                static std::atomic<uint32_t> last{0xFFFFFFFFu};
                const uint32_t key = (irqp & 1u) | (icip ^ (icmr * 2654435761u));
                if (last.exchange(key, std::memory_order_relaxed) == key &&
                        (b & 0x3FFFFu) != 0u)
                    return;
                LOG(Trace, "[IRQSTUCK] irqp=%u I=%u ICIP=0x%08X ICMR=0x%08X "
                           "ICPR=0x%08X now=%u nextWake=%u\n",
                    irqp, (c.cpsr >> 7) & 1u, icip, icmr, icpr,
                    c.ReadVa32(0x822B90B4u).value_or(0u),
                    c.ReadVa32(0x822B8E7Cu).value_or(0u));
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(Falcon4220PagingLivelockDiag);

#endif  // CERF_DEV_MODE
