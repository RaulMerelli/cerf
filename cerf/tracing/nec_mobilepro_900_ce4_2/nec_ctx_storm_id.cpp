#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"
#include "bundle.h"

#include <chrono>
#include <cstdint>

/* IDA-verified in ce4_2 nk.exe (garbage if changed un-rederived): sub_90205BA8 =
   scheduler address-space switch, R0 = incoming THREAD; thread+0xC = process,
   thread+0xA0 = saved PC (resume site); process byte 0 = procnum (slot), +0x20 =
   EXE name. Offsets from scheduler sub_9020912C / abort formatter sub_90209800. */
namespace {

constexpr uint32_t kSchedEntryPc  = 0x90205BA8u;
constexpr uint32_t kThrProcOff    = 0x0Cu;
constexpr uint32_t kThrResumePcOff= 0xA0u;
constexpr uint32_t kProcNameOff   = 0x20u;

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

class TraceNecCtxStormId : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [this, &tm] {
            tm.OnPc(kSchedEntryPc, [this](const TraceContext& c) {
                const uint32_t thread = c.regs[0];
                auto proc = c.ReadVa32(thread + kThrProcOff);
                if (!proc) return;
                auto pn = c.ReadVa8(*proc);
                if (!pn) return;
                const uint32_t slot = *pn & 0x1Fu;
                ++switches_[slot];
                if (auto pc = c.ReadVa32(thread + kThrResumePcOff))
                    RecordPc(slot, *pc);
                if (!resolved_[slot]) TryResolve(c, *proc, slot);
            });
            /* UNFILTERED ON PURPOSE: 0x03F892A0 >= 0x02000000 is the unfolded
               shared-DLL region, one physical code location every process runs
               (why they all spin at the same VA) — a PID filter would wrongly drop
               fires. One-shot: dump the code window + regs to ID the module. */
            tm.OnPc(0x9020FC60u, [this](const TraceContext& c) {
                ++wait_calls_;
                const uint32_t to = c.regs[0];
                if (to == 0) ++wait_t0_;
                else if (to <= 16) ++wait_tsmall_;
                else ++wait_tbig_;
            });
            /* PSL resolver sub_90207C68 (every cross-process call dispatches here).
               IDA: a1=R0; a1[0] (R0+0) = call selector, a1[5] (R0+20) = handle arg. */
            tm.OnPc(0x90207C68u, [this](const TraceContext& c) {
                auto sel = c.ReadVa32(c.regs[0]);
                if (!sel) return;
                auto arg = c.ReadVa32(c.regs[0] + 20u);
                RecordPsl(*sel, arg ? *arg : 0u);
            });
            /* PSL dispatch BX R12 (sub_90205448:0x902055A4, fed by MOV R12,R0 at
               0x90205598 = sub_90207C68 return): R12 = resolved target fn. Histogram
               it to NAME the dominant selector's target (the storm selector
               outweighs all others ~50:1, so its target is top1). */
            tm.OnPc(0x902055A4u, [this](const TraceContext& c) {
                RecordPslTgt(c.regs[12]);
            });
            /* coredll GetTickCount entry. UNFILTERED ON PURPOSE: the storm is
               system-wide, so a PID filter would hide the breadth being measured.
               LR = spin-loop body; per-fire FCSE slot IDs the spinning process. */
            tm.OnPc(0x03F84838u, [this](const TraceContext& c) {
                const uint32_t lr = c.regs[14];
                const uint32_t slot =
                    (c.emu.Get<ArmMmu>().State()->process_id >> 25) & 0x7Fu;
                if (RecordGtc(lr, slot) && gtc_dumps_ < 48u) {
                    ++gtc_dumps_;
                    auto w = [&](uint32_t a) { auto v = c.ReadVa32(a); return v ? *v : 0u; };
                    LOG(Trace, "[GTCASM] lr=0x%08X slot=%u: %08X %08X [%08X] %08X %08X\n",
                        lr, slot, w(lr - 12u), w(lr - 8u), w(lr - 4u), w(lr), w(lr + 4u));
                }
            });
            tm.OnRunLoopIter([this](const TraceContext& c) {
                RecordRunPc(c.regs[15]);
                Dump();
            });
        });
    }

private:
    void TryResolve(const TraceContext& c, uint32_t proc, uint32_t slot) {
        auto np = c.ReadVa32(proc + kProcNameOff);
        if (!np) return;
        name_ptr_[slot] = *np;
        if (*np == 0) { Lock(slot, "<null>"); return; }
        char buf[kNameMax];
        uint32_t i = 0;
        for (; i < kNameMax - 1u; ++i) {
            auto w = c.ReadVa16(*np + i * 2u);
            if (!w) return;
            if (*w == 0) break;
            buf[i] = (*w < 0x80u) ? static_cast<char>(*w) : '?';
        }
        buf[i] = '\0';
        for (uint32_t k = 0; k <= i; ++k) name_[slot][k] = buf[k];
        Lock(slot, name_[slot]);
    }

    void Lock(uint32_t slot, const char* name) {
        resolved_[slot] = true;
        LOG(Trace, "[SLOTMAP] slot=%u pid=0x%08X nameptr=0x%08X name='%s'\n",
            slot, slot << 25, name_ptr_[slot], name);
    }

    void RecordPc(uint32_t slot, uint32_t pc) {
        for (uint32_t k = 0; k < kPcWays; ++k)
            if (pu_[slot][k] && pv_[slot][k] == pc) { ++pc_[slot][k]; return; }
        uint32_t mk = 0;
        uint64_t mc = ~0ull;
        for (uint32_t k = 0; k < kPcWays; ++k) {
            if (!pu_[slot][k]) { pu_[slot][k] = true; pv_[slot][k] = pc; pc_[slot][k] = 1; return; }
            if (pc_[slot][k] < mc) { mc = pc_[slot][k]; mk = k; }
        }
        pv_[slot][mk] = pc;
        pc_[slot][mk] = 1;
    }

    void RecordPsl(uint32_t sel, uint32_t arg) {
        const uint32_t mix = sel * 2654435761u;
        for (uint32_t p = 0; p < 8u; ++p) {
            const uint32_t s = (mix + p) & (kPslHist - 1u);
            if (psl_cnt_[s] != 0 && psl_sel_[s] == sel) { ++psl_cnt_[s]; psl_arg_[s] = arg; return; }
            if (psl_cnt_[s] == 0) { psl_sel_[s] = sel; psl_arg_[s] = arg; psl_cnt_[s] = 1; return; }
        }
    }

    void DumpTopPsl() {
        for (uint32_t n = 0; n < 6u; ++n) {
            uint32_t best = 0;
            uint64_t bc = 0;
            for (uint32_t s = 0; s < kPslHist; ++s)
                if (psl_cnt_[s] > bc) { bc = psl_cnt_[s]; best = s; }
            if (!bc) break;
            LOG(Trace, "[PSLID] top%u sel=0x%08X arg=0x%08X count=%llu\n",
                n + 1, psl_sel_[best], psl_arg_[best],
                static_cast<unsigned long long>(bc));
            psl_cnt_[best] = 0;
        }
        for (uint32_t s = 0; s < kPslHist; ++s) psl_cnt_[s] = 0;
    }

    /* Returns true exactly once per LR, when its running count crosses 5000 —
       isolates the storm's hot loop(s) so the byte-signature dump skips the
       boot noise and fires while the loop's page is TLB-resident. */
    bool RecordGtc(uint32_t lr, uint32_t slot) {
        const uint32_t mix = lr * 2654435761u;
        for (uint32_t p = 0; p < 8u; ++p) {
            const uint32_t s = (mix + p) & (kGtcHist - 1u);
            if (gtc_cnt_[s] != 0 && gtc_lr_[s] == lr) {
                gtc_slot_[s] = slot;
                return ++gtc_cnt_[s] == 5000u;
            }
            if (gtc_cnt_[s] == 0) { gtc_lr_[s] = lr; gtc_slot_[s] = slot; gtc_cnt_[s] = 1; return false; }
        }
        return false;
    }

    void DumpTopGtc() {
        for (uint32_t n = 0; n < 6u; ++n) {
            uint32_t best = 0;
            uint64_t bc = 0;
            for (uint32_t s = 0; s < kGtcHist; ++s)
                if (gtc_cnt_[s] > bc) { bc = gtc_cnt_[s]; best = s; }
            if (!bc) break;
            LOG(Trace, "[GTCLR] top%u lr=0x%08X slot=%u count=%llu\n",
                n + 1, gtc_lr_[best], gtc_slot_[best],
                static_cast<unsigned long long>(bc));
            gtc_cnt_[best] = 0;
        }
        for (uint32_t s = 0; s < kGtcHist; ++s) gtc_cnt_[s] = 0;
    }

    void RecordPslTgt(uint32_t fn) {
        const uint32_t mix = fn * 2654435761u;
        for (uint32_t p = 0; p < 8u; ++p) {
            const uint32_t s = (mix + p) & (kTgtHist - 1u);
            if (tgt_cnt_[s] != 0 && tgt_fn_[s] == fn) { ++tgt_cnt_[s]; return; }
            if (tgt_cnt_[s] == 0) { tgt_fn_[s] = fn; tgt_cnt_[s] = 1; return; }
        }
    }

    void DumpTopPslTgt() {
        for (uint32_t n = 0; n < 4u; ++n) {
            uint32_t best = 0;
            uint64_t bc = 0;
            for (uint32_t s = 0; s < kTgtHist; ++s)
                if (tgt_cnt_[s] > bc) { bc = tgt_cnt_[s]; best = s; }
            if (!bc) break;
            LOG(Trace, "[PSLTGT] top%u fn=0x%08X count=%llu\n",
                n + 1, tgt_fn_[best], static_cast<unsigned long long>(bc));
            tgt_cnt_[best] = 0;
        }
        for (uint32_t s = 0; s < kTgtHist; ++s) tgt_cnt_[s] = 0;
    }

    void RecordRunPc(uint32_t pc) {
        const uint32_t mix = pc * 2654435761u;
        for (uint32_t p = 0; p < 8u; ++p) {
            const uint32_t s = (mix + p) & (kRunHist - 1u);
            if (rpc_cnt_[s] != 0 && rpc_pc_[s] == pc) { ++rpc_cnt_[s]; return; }
            if (rpc_cnt_[s] == 0) { rpc_pc_[s] = pc; rpc_cnt_[s] = 1; return; }
        }
    }

    void DumpTopRunPcs() {
        for (uint32_t n = 0; n < 6u; ++n) {
            uint32_t best = 0;
            uint64_t bc = 0;
            for (uint32_t s = 0; s < kRunHist; ++s)
                if (rpc_cnt_[s] > bc) { bc = rpc_cnt_[s]; best = s; }
            if (!bc) break;
            LOG(Trace, "[RUNPC] top%u pc=0x%08X count=%llu\n",
                n + 1, rpc_pc_[best], static_cast<unsigned long long>(bc));
            rpc_cnt_[best] = 0;
        }
        for (uint32_t s = 0; s < kRunHist; ++s) rpc_cnt_[s] = 0;
    }

    void Dump() {
        const long long now = NowMs();
        if (now - last_ms_ < 1000) return;
        last_ms_ = now;
        DumpTopRunPcs();
        DumpTopPsl();
        DumpTopPslTgt();
        DumpTopGtc();
        LOG(Trace, "[WAITRATE] sub_9020FC60/sec=%llu timeout0=%llu small(1-16)=%llu "
            "big=%llu\n",
            static_cast<unsigned long long>(wait_calls_),
            static_cast<unsigned long long>(wait_t0_),
            static_cast<unsigned long long>(wait_tsmall_),
            static_cast<unsigned long long>(wait_tbig_));
        wait_calls_ = wait_t0_ = wait_tsmall_ = wait_tbig_ = 0;
        for (uint32_t s = 0; s < 32; ++s) {
            const uint64_t n = switches_[s];
            switches_[s] = 0;
            uint32_t bk = 0;
            uint64_t bc = 0;
            for (uint32_t k = 0; k < kPcWays; ++k)
                if (pu_[s][k] && pc_[s][k] > bc) { bc = pc_[s][k]; bk = k; }
            if (n >= 200)
                LOG(Trace, "[CTXSTORM] slot=%u name='%s' switches/sec=%llu "
                    "topresumepc=0x%08X cnt=%llu\n",
                    s, resolved_[s] ? name_[s] : "?",
                    static_cast<unsigned long long>(n),
                    bc ? pv_[s][bk] : 0, static_cast<unsigned long long>(bc));
            for (uint32_t k = 0; k < kPcWays; ++k) { pu_[s][k] = false; pc_[s][k] = 0; }
        }
    }

    static constexpr uint32_t kNameMax = 24;
    static constexpr uint32_t kPcWays  = 4;
    uint64_t  switches_[32] = {};
    bool      resolved_[32] = {};
    char      name_[32][kNameMax] = {};
    uint32_t  name_ptr_[32] = {};
    uint64_t  wait_calls_ = 0;
    uint64_t  wait_t0_ = 0;
    uint64_t  wait_tsmall_ = 0;
    uint64_t  wait_tbig_ = 0;
    static constexpr uint32_t kRunHist = 512;
    uint32_t  rpc_pc_[kRunHist] = {};
    uint64_t  rpc_cnt_[kRunHist] = {};
    static constexpr uint32_t kPslHist = 256;
    uint32_t  psl_sel_[kPslHist] = {};
    uint32_t  psl_arg_[kPslHist] = {};
    uint64_t  psl_cnt_[kPslHist] = {};
    static constexpr uint32_t kTgtHist = 128;
    uint32_t  tgt_fn_[kTgtHist] = {};
    uint64_t  tgt_cnt_[kTgtHist] = {};
    static constexpr uint32_t kGtcHist = 256;
    uint32_t  gtc_lr_[kGtcHist] = {};
    uint32_t  gtc_slot_[kGtcHist] = {};
    uint64_t  gtc_cnt_[kGtcHist] = {};
    uint32_t  gtc_dumps_ = 0;
    bool      pu_[32][kPcWays] = {};
    uint32_t  pv_[32][kPcWays] = {};
    uint64_t  pc_[32][kPcWays] = {};
    long long last_ms_ = 0;
};

REGISTER_SERVICE(TraceNecCtxStormId);

}  /* namespace */
