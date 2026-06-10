#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_cpu.h"
#include "../../jit/arm_mmu.h"
#include "bundle.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <set>
#include <thread>

#if CERF_DEV_MODE

namespace {

/* The SIMpad "deep sleep" ~10s into boot is CE's panic/power-off (PowerOffSystem
   sub_8008810C) after heavy aborts — the same shape seen on Falcon 4220; no
   device sleeps immediately after booting. This probe finds the aborts driving
   the panic and dumps the eventual halted state. */
class SimpadSl4IrqDeliveryHangWatchdog : public Service {
public:
    using Service::Service;

    void OnShutdown() override {
        stop_.store(true, std::memory_order_release);
        if (thread_.joinable()) thread_.join();
    }

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            /* Every exception the kernel dispatcher (sub_80093650) handles goes
               through BL sub_800A0680 at 0x80093C18 with a2=excType (0=normal
               reschedule), a3=FAR, a1=thread. Log each distinct (excType, FAR,
               faultPC) to surface the recurring fault behind the panic. */
            auto seen    = std::make_shared<std::set<uint64_t>>();
            auto aborts  = std::make_shared<uint64_t>(0);
            auto resched = std::make_shared<uint64_t>(0);
            tm.OnPc(0x80093C18u, [seen, aborts, resched](const TraceContext& c) {
                const uint32_t exc = c.regs[1];
                if (exc == 0) {       /* a2==0 = normal reschedule, not an abort */
                    if (++*resched % 100000ull == 0)
                        LOG(Caution, "[SCHED] resched=%llu aborts=%llu\n",
                            (unsigned long long)*resched, (unsigned long long)*aborts);
                    return;
                }
                ++*aborts;
                const uint32_t a1  = c.regs[0];
                const uint32_t far = c.regs[2];
                const uint32_t fpc = c.ReadVa32(a1 + 160u).value_or(0xDEAD0000u);
                /* Key on (excType, FAR) only — IRQ entries carry a stale FAR so
                   they collapse to one, while distinct abort addresses surface. */
                const uint64_t key = (static_cast<uint64_t>(exc) << 32) ^ far;
                if (seen->size() < 128u && seen->insert(key).second)
                    LOG(Caution, "[ABORT] aborts=%llu excType=%u FAR=0x%08X "
                                 "faultPC=0x%08X distinct=%zu\n",
                        (unsigned long long)*aborts, exc, far, fpc, seen->size());
                else if (*aborts % 100000ull == 0)
                    LOG(Caution, "[ABORT] aborts=%llu resched=%llu distinct=%zu\n",
                        (unsigned long long)*aborts, (unsigned long long)*resched,
                        seen->size());
            });

            /* sub_80087F1C powers off iff caller context *(MEMORY[0xFFFFC890]+3)==2.
               Walk pModList to name LR's module: head=MEMORY[0xFFFFCB24],
               MODULE.next=+4 name=+8(LPWSTR) base=+84 (CE4 loader sub_8009E8E4). */
            tm.OnPc(0x80087F1Cu, [](const TraceContext& c) {
                const uint32_t lr   = c.regs[14];
                const uint32_t cur  = c.ReadVa32(0xFFFFC890u).value_or(0u);
                const uint32_t crit = cur ? c.ReadVa8(cur + 3u).value_or(0xFFu) : 0xFFu;
                uint32_t best_base = 0;
                char best_name[64] = "?";
                uint32_t m = c.ReadVa32(0xFFFFCB24u).value_or(0u);
                for (int i = 0; i < 512 && m; ++i) {
                    const uint32_t base = c.ReadVa32(m + 84u).value_or(0u);
                    if (base && base <= lr && base > best_base) {
                        best_base = base;
                        const uint32_t np = c.ReadVa32(m + 8u).value_or(0u);
                        int j = 0;
                        for (; np && j < 63; ++j) {
                            auto w = c.ReadVa16(np + 2u * j);
                            if (!w || *w == 0) break;
                            best_name[j] = (*w >= 0x20 && *w < 0x7F) ? char(*w) : '?';
                        }
                        best_name[j] = 0;
                    }
                    m = c.ReadVa32(m + 4u).value_or(0u);
                }
                LOG(Caution, "[PANIC] decision pc=0x%08X lr=0x%08X crit=%u "
                             "(2=>PowerOff) module='%s' base=0x%08X off=0x%X\n",
                    c.pc, lr, crit, best_name, best_base, lr - best_base);
            });

            /* coredll!PowerOffSystem entry (0x03F84CF8, shared-DLL VA == runtime).
               UNFILTERED ON PURPOSE: any process may request power-off and naming
               which one is the goal — pid is logged for attribution, LR matched
               against [MODMAP] to name the caller's module. */
            tm.OnPc(0x03F84CF8u, [](const TraceContext& c) {
                LOG(Caution, "[POWEROFF] PowerOffSystem() pid=0x%08X lr=0x%08X "
                             "r0=0x%08X r1=0x%08X\n",
                    c.emu.Get<ArmMmu>().State()->process_id, c.regs[14],
                    c.regs[0], c.regs[1]);
            });

            /* gwes sub_AB9D0 (0xABA54, after LDRH): R0 = the HKLM\SOFTWARE\Version
               word the battery monitor uses to pick its mV thresholds (v4==83 ->
               v7=7200/v8=8200, else v7=6500/v8=8000). Reads the guest's actual range. */
            tm.OnPc(0x000ABA54u, [](const TraceContext& c) {
                if (c.emu.Get<ArmMmu>().State()->process_id != 0x0A000000u) return;
                LOG(Caution, "[BATTVAR] gwes Version word=%u (0x%04X)\n",
                    c.regs[0] & 0xFFFFu, c.regs[0] & 0xFFFFu);
            });

            /* coredll!GwesPowerOffSystem (0x03F7E008) thunk entry: catches the
               external requester before its PSL trap. UNFILTERED ON PURPOSE (any
               caller) — LR+pid name who requested the suspend (match LR to [MODMAP]). */
            tm.OnPc(0x03F7E008u, [](const TraceContext& c) {
                LOG(Caution, "[GPOFFREQ] GwesPowerOffSystem caller lr=0x%08X pid=0x%08X\n",
                    c.regs[14], c.emu.Get<ArmMmu>().State()->process_id);
            });

            /* gwes.exe sub_155C8 (suspend helper) entry: LR distinguishes the
               trigger path — 0x1ABAC = power-KEY handler sub_1A8EC (GPIO->vkey),
               0x2CB78 = IDLE-timer thread sub_2C930. gwes-only code (pid logged). */
            tm.OnPc(0x000155C8u, [](const TraceContext& c) {
                const uint32_t pid = c.emu.Get<ArmMmu>().State()->process_id;
                if (pid != 0x0A000000u) return;   /* gwes only; skip slot-0 aliases */
                /* PSL callstack: pCurThd[6] (pCurThd+24)=pcstk head; frame+4 =
                   client return addr (sub_800AD120 v4[1]=a1[4]). Names the client. */
                const uint32_t thd  = c.ReadVa32(0xFFFFC894u).value_or(0u);
                const uint32_t cstk = thd ? c.ReadVa32(thd + 24u).value_or(0u) : 0u;
                const uint32_t cret = cstk ? c.ReadVa32(cstk + 4u).value_or(0u) : 0u;
                LOG(Caution, "[SUSPSRC] gwes sub_155C8 lr=0x%08X cstk=0x%08X "
                             "clientRet=0x%08X f0=0x%08X f8=0x%08X\n",
                    c.regs[14], cstk, cret,
                    cstk ? c.ReadVa32(cstk).value_or(0u) : 0u,
                    cstk ? c.ReadVa32(cstk + 8u).value_or(0u) : 0u);
            });

            /* coredll!SetSystemPowerState (0x03F9DC20) thunk: catches the requester
               in ITS OWN process before the PSL trap. UNFILTERED ON PURPOSE (any
               requester) — lr+pid name who asked to suspend (match LR to [MODMAP]).
               a2=flags (0x200000=SUSPEND), a3=options. */
            tm.OnPc(0x03F9DC20u, [](const TraceContext& c) {
                LOG(Caution, "[SPSREQ] SetSystemPowerState flags=0x%08X opt=0x%08X "
                             "lr=0x%08X pid=0x%08X\n",
                    c.regs[1], c.regs[2], c.regs[14],
                    c.emu.Get<ArmMmu>().State()->process_id);
            });

            /* pm.dll SetSystemPowerState core sub_3F5296C: a1 = state-name string.
               UNFILTERED ON PURPOSE (PM API, any requester) — dump the requested
               state name + flags + caller LR to learn WHY the PM powers off. */
            tm.OnPc(0x03F5296Cu, [](const TraceContext& c) {
                const uint32_t a1 = c.regs[0];
                char st[48];
                int j = 0;
                for (; a1 && j < 47; ++j) {
                    auto w = c.ReadVa16(a1 + 2u * j);
                    if (!w || *w == 0) break;
                    st[j] = (*w >= 0x20 && *w < 0x7F) ? char(*w) : '?';
                }
                st[j] = 0;
                LOG(Caution, "[PMSTATE] SetSystemPowerState name='%s' flags=0x%08X "
                             "lr=0x%08X pid=0x%08X\n",
                    a1 ? st : "(byid)", c.regs[1], c.regs[14],
                    c.emu.Get<ArmMmu>().State()->process_id);
            });

            /* Loader epilogue: 0x8009EF64 (MOV R0,R10) returns the module ptr in
               R10; structs are hot here (the panic-time walk reads them cold).
               Dump base->name once per module to match the panic LR offline. */
            auto modseen = std::make_shared<std::set<uint32_t>>();
            tm.OnPc(0x8009EF64u, [modseen](const TraceContext& c) {
                const uint32_t mod = c.regs[10];
                if (!mod) return;
                const uint32_t base = c.ReadVa32(mod + 84u).value_or(0u);
                if (!base || !modseen->insert(base).second) return;
                const uint32_t np = c.ReadVa32(mod + 8u).value_or(0u);
                char name[64];
                int j = 0;
                for (; np && j < 63; ++j) {
                    auto w = c.ReadVa16(np + 2u * j);
                    if (!w || *w == 0) break;
                    name[j] = (*w >= 0x20 && *w < 0x7F) ? char(*w) : '?';
                }
                name[j] = 0;
                LOG(Caution, "[MODMAP] base=0x%08X name='%s'\n", base, name);
            });

            thread_ = std::thread([this] { WatchLoop(); });
        });
    }

private:
    std::atomic<bool> stop_{false};
    std::thread       thread_;

    void WatchLoop() {
        const ArmCpuState* state = emu_.Get<ArmCpu>().State();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        uint32_t prev_cycles = state->guest_cycle_counter;
        while (!stop_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const uint32_t cycles = state->guest_cycle_counter;
            const uint32_t dcyc   = cycles - prev_cycles;
            prev_cycles = cycles;
            LOG(Trace, "[STATE] dcyc=%u cpsr=0x%08X I=%u mode=0x%X pend=%u "
                       "R15=0x%08X LR=0x%08X SP=0x%08X\n",
                dcyc, state->cpsr.partial_word, state->cpsr.bits.irq_disable,
                state->cpsr.bits.mode, state->irq_interrupt_pending,
                state->gprs[15], state->gprs[14], state->gprs[13]);
        }
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4IrqDeliveryHangWatchdog);

#endif  // CERF_DEV_MODE
