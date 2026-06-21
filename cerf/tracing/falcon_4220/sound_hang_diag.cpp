#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"
#include "bundle.h"

#include <atomic>
#include <optional>

#if CERF_DEV_MODE

namespace {

/* S14 consumer thread, learned at WFMO-call 0x39E4C (= pCurThread @ 0xFFFFC894).
   The make-runnable wake (sub_800C5E8C/sub_800C5928) runs in SVC mode with IRQs
   enabled; an added instruction there moves the IRQ-in-wake race that is this bug
   and masks it (S13 Heisenbug) - so its outcome is polled host-side, not hooked. */
std::atomic<uint32_t> g_consumerThread{0};

/* Paint/animation thread, learned at the GPE blit core sub_184B308 (DDI.DLL,
   GWES-only) where it is the running thread. The render hang stops blits while
   the input consumer still limps, so this - not the consumer - is the primary
   freeze; polled below for runnable-but-not-scheduled vs blocked-on-wait. */
std::atomic<uint32_t> g_paintThread{0};

/* Shared-XIP system-DLL code (coredll/waveapi/wavedev): unfiltered on purpose,
   each line logs slot - the consumer PSL-traps GWES(5)->device.exe(3), so a
   process filter would drop the slot-3 fires that locate the wedge. */
class Falcon4220SoundHangDiag : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            auto slot = [](const TraceContext& c) {
                return c.emu.Get<ArmMmu>().State()->process_id >> 25;
            };

            /* coredll!sndPlaySoundW(a1,a2): the consumer's STOP is (0,0x80000000)
               from gwes sub_3057C (LR~0x305xx); the PLAY is (name,0x10001) from
               sub_30488 (LR~0x304xx). If only the STOP logs for a click, the
               wedge is the stop; if both, it is the play. */
            tm.OnPc(0x3F8997Cu, [slot](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] sndPlaySoundW a1=0x%08X a2=0x%08X slot=%u LR=0x%08X\n",
                    c.regs[0], c.regs[1], slot(c), c.regs[14]);
            });

            /* waveapi!WAM_IOControl(a1,ioctl=R1): 0x1D0004 is sndPlaySound. */
            tm.OnPc(0x3D82698u, [slot](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] WAM_IOControl ioctl=0x%08X slot=%u LR=0x%08X\n",
                    c.regs[1], slot(c), c.regs[14]);
            });

            /* waveapi PlaySound core sub_3D84CE8(a1=R0 name/0, flags=R1). */
            tm.OnPc(0x3D84CE8u, [slot](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] sndcore a1=0x%08X flags=0x%08X slot=%u\n",
                    c.regs[0], c.regs[1], slot(c));
            });
            /* reaches the waveOutReset of the previous sound. */
            tm.OnPc(0x3D84F18u, [slot](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] sndcore->waveOutReset(prev) slot=%u\n", slot(c));
            });
            /* the WaitForSingleObject(prevSound+0x44, INFINITE) RETURNED. */
            tm.OnPc(0x3D84F30u, [slot](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] sndcore WFSO(prev) returned r0=0x%08X slot=%u\n",
                    c.regs[0], slot(c));
            });
            /* sndcore STOP path: MEMORY[0x1FD2440] active-sound-stop callback
               (BX R4). R4 = the callback fn ptr; logging it identifies where the
               stop actually goes (the wedge is past here). */
            tm.OnPc(0x3D84D68u, [slot](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] sndcore stop-callback fn=0x%08X r1=0x%08X r2=0x%08X slot=%u\n",
                    c.regs[4], c.regs[1], c.regs[2], slot(c));
            });
            /* One-shot byte signature of the persistent slot-3 wait LR=0x03A7AE94
               (the BL WaitForSingleObject is at 0x03A7AE90) so the module can be
               matched in IDA by .text bytes (no audio DLL loads at this VA). */
            tm.OnPc(0x03A7AE94u, [](const TraceContext& c) {
                static std::atomic<bool> done{false};
                if (done.exchange(true)) return;
                LOG(Trace, "[SNDHANG] wait-0x03A7AE94 bytes "
                    "%08X %08X %08X %08X %08X %08X\n",
                    c.ReadVa32(0x03A7AE84u).value_or(0), c.ReadVa32(0x03A7AE88u).value_or(0),
                    c.ReadVa32(0x03A7AE8Cu).value_or(0), c.ReadVa32(0x03A7AE90u).value_or(0),
                    c.ReadVa32(0x03A7AE94u).value_or(0), c.ReadVa32(0x03A7AE98u).value_or(0));
            });

            /* wavedev!WAV_IOControl(a1,ioctl=R1, a3=R2): wave message = a3[1]. */
            tm.OnPc(0x1905B5Cu, [slot](const TraceContext& c) {
                auto msg = c.ReadVa32(c.regs[2] + 4u);
                LOG(Trace, "[SNDHANG] WAV_IOControl ioctl=0x%08X msg=%d slot=%u LR=0x%08X\n",
                    c.regs[1], static_cast<int>(msg.value_or(0xFFFFFFFFu)),
                    slot(c), c.regs[14]);
            });

            /* GWES touch consumer dequeue (gwes.exe sub_39D20): per-process VAs,
               filtered to GWES (slot 5) - without the filter these alias other
               processes' code at the same VA and the fires are unattributable. */
            const TracePredicate in_gwes = [](const TraceContext& c) {
                return (c.emu.Get<ArmMmu>().State()->process_id >> 25) == 5u;
            };
            tm.OnPcFiltered(0x39D20u, in_gwes, [](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] deq-enter free=%d\n",
                    static_cast<int>(c.ReadVa32(0xB9FE8u).value_or(0xFFFFFFFFu)));
            });
            tm.OnPcFiltered(0x39D70u, in_gwes, [](const TraceContext&) {
                LOG(Trace, "[SNDHANG] deq-got-cs\n");
            });
            tm.OnPcFiltered(0x39E4Cu, in_gwes, [](const TraceContext& c) {
                /* About to WFMO -> the running thread IS the consumer; KData
                   pCurThread (0xFFFFC894) is its kernel thread object. */
                const uint32_t cur = c.ReadVa32(0xFFFFC894u).value_or(0u);
                if (cur >= 0x80000000u)
                    g_consumerThread.store(cur, std::memory_order_relaxed);
                LOG(Trace, "[SNDHANG] deq-wfmo-call timeout=%d evt=0x%08X curthread=0x%08X\n",
                    static_cast<int>(c.regs[3]),
                    c.ReadVa32(0xB9FD4u).value_or(0xFFFFFFFFu), cur);
            });
            tm.OnPcFiltered(0x39D9Cu, in_gwes, [](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] deq-pendsound name=0x%08X\n", c.regs[0]);
            });
            tm.OnPcFiltered(0x39E50u, in_gwes, [](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] deq-wfmo-ret r0=%d\n",
                    static_cast<int>(c.regs[0]));
            });
            tm.OnPcFiltered(0x3A02Cu, in_gwes, [](const TraceContext&) {
                LOG(Trace, "[SNDHANG] deq-hasitem (dequeue)\n");
            });

            /* EventModify stub (0x3F8313C, shared XIP) dispatches through
               MEMORY[0x1FFFAA0] = kernel EventModify ptr; apiset MEMORY[0x1FFFAA8]
               +232 = WFMO target, +604 = reschedule hook. One-shot capture to
               locate the kernel wake path for static decompile. */
            tm.OnPc(0x3F8313Cu, [](const TraceContext& c) {
                static std::atomic<bool> done{false};
                if (done.exchange(true)) return;
                const uint32_t apiset = c.ReadVa32(0x1FFFAA8u).value_or(0);
                LOG(Trace, "[SNDHANG] kfns EventModify=0x%08X apiset=0x%08X "
                           "WFMO=0x%08X postcall=0x%08X\n",
                    c.ReadVa32(0x1FFFAA0u).value_or(0), apiset,
                    c.ReadVa32(apiset + 232u).value_or(0),
                    c.ReadVa32(apiset + 604u).value_or(0));
            });

            /* Kernel EventModify sub_800C6AAC; the ring-event filter
               (R0 == handle dword_B9FD4) is load-bearing - unfiltered it fires
               per-SET and the perturbation masks the hang. R1 = 3 SET / 2 RESET. */
            const TracePredicate is_ring = [](const TraceContext& c) {
                auto h = c.ReadVa32(0xB9FD4u);
                return h.has_value() && c.regs[0] == *h;
            };
            tm.OnPcFiltered(0x800C6AACu, is_ring, [](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] kRingEvtMod type=%d LR=0x%08X\n",
                    static_cast<int>(c.regs[1]), c.regs[14]);
            });

            /* 0x800C6B3C = manual-reset SET branch (R7=event, R4=handle); event+4
               is the wait-block list sub_800CAE24 writes and sub_800C667C wakes
               from. wlist==0 at a ring SET = consumer not linked (enqueue side). */
            const TracePredicate is_ring_r4 = [](const TraceContext& c) {
                auto h = c.ReadVa32(0xB9FD4u);
                return h.has_value() && c.regs[4] == *h;
            };
            tm.OnPcFiltered(0x800C6B3Cu, is_ring_r4, [](const TraceContext& c) {
                LOG(Trace, "[SNDHANG] kRingWaiter wlist=0x%08X type=%d\n",
                    c.ReadVa32(c.regs[7] + 4u).value_or(0xFFFFFFFFu),
                    static_cast<int>(c.regs[8]));
            });

            /* GWES global lock stru_B9920: the consumer blocks here in sub_20FA0's
               EnterCriticalSection (gwes sub_15820) when held. CE 4.2 CRITICAL_SECTION
               (coredll): +0 LockCount, +4 OwnerThreadId vs KData curId 0xFFFFC808.
               Read in gwes context - the VA aliases per process; log holder on contention. */
            tm.OnPcFiltered(0x15820u, in_gwes, [](const TraceContext& c) {
                const uint32_t owner = c.ReadVa32(0xB9924u).value_or(0u);
                const uint32_t curId = c.ReadVa32(0xFFFFC808u).value_or(0u);
                if (owner == 0u || owner == curId) return;   /* free / recursive. */
                LOG(Trace, "[SNDHANG] B9920-contend curId=0x%08X owner=0x%08X lock=%u LR=0x%08X\n",
                    curId, owner, c.ReadVa32(0xB9920u).value_or(0u), c.regs[14]);
            });

            /* Consumer status low-2-bits (written by sub_800C5928) name the gate:
               01 = enqueued runnable (lost-wake is downstream resched/priority),
               10 = thread+2 gate skipped the run-queue insert, 11 = unchanged
               (never reached sub_800C5928 - sub_800C5E8C status!=3). */
            tm.OnRunLoopIter([](const TraceContext& c) {
                const uint32_t th = g_consumerThread.load(std::memory_order_relaxed);
                if (th == 0u) return;
                auto st = c.ReadVa16(th);
                if (!st) return;   /* thread page not data-TLB-hot this iter. */
                const uint32_t low2 = *st & 3u;
                const uint32_t tp2  = c.ReadVa8(th + 2u).value_or(0xFFu);

                /* Consumer runnable (low2==1) but maybe never dispatched: snapshot
                   who runs instead, priorities (+0x41, lower=higher; sub_800C5928),
                   run-queue head 0x822B8EC0, consumer links (+336/+340). curPrio>=
                   consPrio yet consumer not picked = reschedule bug; < = legit preempt. */
                if (low2 == 1u) {
                    const uint32_t cur = c.ReadVa32(0xFFFFC894u).value_or(0u);
                    static std::atomic<uint32_t> lastCur{0};
                    static std::atomic<uint32_t> beat{0};
                    const uint32_t b = beat.fetch_add(1, std::memory_order_relaxed);
                    if (cur != lastCur.exchange(cur, std::memory_order_relaxed) ||
                        (b & 0x3FFFu) == 0u) {
                        LOG(Trace, "[SNDHANG] runnable-snap cons=0x%08X consPrio=%u cur=0x%08X "
                                   "curPrio=%u rqHead=0x%08X rqCur=0x%08X cNext=0x%08X cPrev=0x%08X\n",
                            th, c.ReadVa8(th + 0x41u).value_or(0xFFu), cur,
                            cur ? c.ReadVa8(cur + 0x41u).value_or(0xFFu) : 0xFFu,
                            c.ReadVa32(0x822B8EC0u).value_or(0u),
                            c.ReadVa32(0x822B8EC4u).value_or(0u),
                            c.ReadVa32(th + 336u).value_or(0u),
                            c.ReadVa32(th + 340u).value_or(0u));
                    }
                }

                static std::atomic<uint32_t> last{0xFFFFFFFFu};
                const uint32_t key = (low2 << 8) | tp2;
                if (last.exchange(key, std::memory_order_relaxed) == key) return;
                LOG(Trace, "[SNDHANG] mkrun-state thread=0x%08X status=0x%04X low2=%u "
                           "tp2=0x%02X r884=0x%08X r898=0x%08X\n",
                    th, static_cast<unsigned>(*st), low2, tp2,
                    c.ReadVa32(0xFFFFC884u).value_or(0xFFFFFFFFu),
                    c.ReadVa32(0xFFFFC898u).value_or(0xFFFFFFFFu));
            });

            /* Learn the paint thread at the GPE blit core (GWES-only running thread). */
            tm.OnPcFiltered(0x184B308u, in_gwes, [](const TraceContext& c) {
                const uint32_t cur = c.ReadVa32(0xFFFFC894u).value_or(0u);
                if (cur >= 0x80000000u)
                    g_paintThread.store(cur, std::memory_order_relaxed);
            });
            /* Classify the paint stall: low2==1 runnable-but-not-scheduled (snap who
               runs instead + priorities) vs low2==2 blocked-on-wait. */
            tm.OnRunLoopIter([](const TraceContext& c) {
                const uint32_t th = g_paintThread.load(std::memory_order_relaxed);
                if (th == 0u) return;
                auto st = c.ReadVa16(th);
                if (!st) return;
                const uint32_t low2 = *st & 3u;
                if (low2 == 1u) {
                    const uint32_t cur = c.ReadVa32(0xFFFFC894u).value_or(0u);
                    static std::atomic<uint32_t> lastCur{0};
                    static std::atomic<uint32_t> beat{0};
                    const uint32_t b = beat.fetch_add(1, std::memory_order_relaxed);
                    const uint32_t rqHead = c.ReadVa32(0x822B8EC0u).value_or(0u);
                    if (cur != lastCur.exchange(cur, std::memory_order_relaxed) ||
                        (b & 0x3FFFu) == 0u)
                        LOG(Trace, "[SNDHANG] paint-snap paint=0x%08X pPrio=%u cur=0x%08X "
                                   "curPrio=%u pc=0x%08X rqHead=0x%08X rqHst=0x%04X rqHprio=%u "
                                   "rqCur=0x%08X r884=%u r898=%u pend=0x%08X\n",
                            th, c.ReadVa8(th + 0x41u).value_or(0xFFu), cur,
                            cur ? c.ReadVa8(cur + 0x41u).value_or(0xFFu) : 0xFFu,
                            c.pc, rqHead,
                            rqHead ? c.ReadVa16(rqHead).value_or(0xFFFFu) : 0xFFFFu,
                            rqHead ? c.ReadVa8(rqHead + 0x41u).value_or(0xFFu) : 0xFFu,
                            c.ReadVa32(0x822B8EC4u).value_or(0u),
                            c.ReadVa32(0xFFFFC884u).value_or(0xFFFFFFFFu),
                            c.ReadVa32(0xFFFFC898u).value_or(0xFFFFFFFFu),
                            c.ReadVa32(0x822B94C0u).value_or(0xFFFFFFFFu));
                }
                static std::atomic<uint32_t> last{0xFFu};
                if (last.exchange(low2, std::memory_order_relaxed) == low2) return;
                LOG(Trace, "[SNDHANG] paint-state thread=0x%08X status=0x%04X low2=%u\n",
                    th, static_cast<unsigned>(*st), low2);
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(Falcon4220SoundHangDiag);

#endif  // CERF_DEV_MODE
