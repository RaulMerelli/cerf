#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"
#include "bundle.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

/* coredll WFSO (R0=handle, R1=timeout) / WFMO (R0=count, R1=lpHandles, R3=timeout);
   LR = guest wait site. These shared-VA exports run in every process; the FCSE slot
   is tagged per call (the hook is intentionally unfiltered, multi-process). */
namespace {

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

class TraceNecWfsoTracker : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [this, &tm] {
            /* WaitForSingleObject(handle=R0, timeout=R1). */
            tm.OnPc(0x03F8353Cu, [this](const TraceContext& c) {
                Log(c, "WFSO", c.regs[0], c.regs[1], c.regs[14]);
            });
            /* WaitForMultipleObjects(count=R0, lpHandles=R1, waitAll=R2,
               timeout=R3) — report the first handle. */
            tm.OnPc(0x03F834C0u, [this](const TraceContext& c) {
                uint32_t h0 = 0;
                if (auto p = c.ReadVa32(c.regs[1])) h0 = *p;
                Log(c, "WFMO", h0, c.regs[3], c.regs[14]);
            });
            /* MsgWaitForMultipleObjectsEx(count=R0, pHandles=R1, timeout=R2,
               wakeMask=R3) — the GUI message-pump wait. */
            tm.OnPc(0x03F7F5E8u, [this](const TraceContext& c) {
                uint32_t h0 = 0;
                if (c.regs[0] != 0) { if (auto p = c.ReadVa32(c.regs[1])) h0 = *p; }
                Log(c, "MWFMO", h0, c.regs[2], c.regs[14]);
            });
            /* Sleep(timeout=R0). */
            tm.OnPc(0x03F83454u, [this](const TraceContext& c) {
                Log(c, "Sleep", 0, c.regs[0], c.regs[14]);
            });
        });
    }

private:
    struct WaitInfo { long long entry; uint32_t caller; uint32_t handle;
                      const char* kind; };

    void Log(const TraceContext& c, const char* kind, uint32_t handle,
             uint32_t timeout, uint32_t lr) {
        if (timeout == 0) return;            /* poll, not a blocking wait */
        const long long now = NowMs();
        /* Per-thread duration: 0xFFFFC894 = kernel pCurThread. When this thread
           enters its next wait, its PREVIOUS wait's blocked-time is known. A wait
           that blocked for the hang length names the culprit when it resumes. */
        uint32_t thread = 0;
        if (auto t = c.ReadVa32(0xFFFFC894u)) thread = *t;
        if (thread) {
            auto it = wait_.find(thread);
            if (it != wait_.end() && now - it->second.entry >= 2000)
                LOG(Trace, "[WAITDUR] thread=0x%08X blocked %lldms in %s "
                    "caller=0x%08X handle=0x%08X\n",
                    thread, now - it->second.entry, it->second.kind,
                    it->second.caller, it->second.handle);
            wait_[thread] = WaitInfo{now, lr, handle, kind};
        }
        /* One-shot byte dump of each long finite-timeout wait site (the
           never-signalled ~30s hang wait is rare; this isolates it) so its DLL is
           byte-matchable against the extracted modules. */
        if (timeout >= 10000u && timeout != 0xFFFFFFFFu &&
            dumps_ < 96u && seen_.insert(lr).second) {
            ++dumps_;
            auto w = [&](uint32_t a) { auto v = c.ReadVa32(a); return v ? *v : 0u; };
            LOG(Trace, "[WAITASM] caller=0x%08X: %08X %08X [%08X] %08X %08X\n",
                lr, w(lr - 12u), w(lr - 8u), w(lr - 4u), w(lr), w(lr + 4u));
        }
        long long& last = last_ms_[lr];
        if (last != 0 && now - last < 2000) return;
        last = now;
        const uint32_t slot =
            (c.emu.Get<ArmMmu>().State()->process_id >> 25) & 0x7Fu;
        LOG(Trace, "[WFSO] slot=%u %s caller=0x%08X handle=0x%08X timeout=%u\n",
            slot, kind, lr, handle, timeout);
    }

    std::unordered_map<uint32_t, long long>  last_ms_;
    std::unordered_map<uint32_t, WaitInfo>   wait_;
    std::unordered_set<uint32_t>             seen_;
    uint32_t                                 dumps_ = 0;
};

REGISTER_SERVICE(TraceNecWfsoTracker);

}  /* namespace */
