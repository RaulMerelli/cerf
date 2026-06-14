#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "sync_2_bundle.h"

#if CERF_DEV_MODE

#include <unordered_map>

/* Ladder across start()'s PA->VA transition (the MMU-enable + MOV-PC-to-virtual).
   Pre-MMU PCs run at PA 0x901xxxxx so those hooks use PA; the post-jump
   continuation runs at the kernel link VA 0x801xxxxx so those hooks use VA.
   Rewriting a PA hook to its VA twin (or vice-versa) makes it never fire. */
namespace {

class Sync2KernelParkTrace : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kBundleCrc32, [this, &tm] {
            Hook(tm, 0x9010578Cu, "epilogue-entry  (PA)");
            Hook(tm, 0x901058C4u, "identity-install(PA)");
            Hook(tm, 0x90105900u, "MMU-enable-MCR  (PA)");
            Hook(tm, 0x90105904u, "post-enable MOVPC(PA)");
            Hook(tm, 0x90105908u, "VStart NOP      (PA)");
            Hook(tm, 0x9010590Cu, "VStart FirstPT  (PA)");
            Hook(tm, 0x90105920u, "VStart r4=KData (PA)");
            Hook(tm, 0x80105908u, "VStart NOP      (VA)");
            Hook(tm, 0x8010590Cu, "VStart FirstPT  (VA)");
            Hook(tm, 0x80105920u, "VStart r4=KData (VA)");
            Hook(tm, 0x8010594Cu, "VA-landing      (VA)");
            Hook(tm, 0x80105968u, "BL kernel-resolve(VA)");
            Hook(tm, 0x80105974u, "MOVPC kernel-ent (VA)");
            Hook(tm, 0x80105994u, "sub_80105994     (VA)");
            Hook(tm, 0x801059A8u, "post-D8          (VA)");
            Hook(tm, 0x80105A18u, "D8->A18 tailcall (VA)");
            Hook(tm, 0x8010F898u, "D8->F898         (VA)");
            Hook(tm, 0x80105A4Cu, "D8-JUMPOUT       (VA)");
            /* Per-LDR ladder inside sub_801059D8 (straight-line, no loop) to
               pin the instruction that fails to advance + the addr it loads. */
            HookPteDump(tm, 0x801059E0u, "D8 LDR[r7+0x20]  (VA)");
            Hook(tm, 0x801059E8u, "D8 CMP r3=[0x20] (VA)");
            Hook(tm, 0x801059F4u, "D8 LDR[r7+0x24]  (VA)");
            Hook(tm, 0x801059FCu, "D8 LDR[r4+8]     (VA)");
            Hook(tm, 0x80105A00u, "D8 CMP r3=[r4+8] (VA)");
            Hook(tm, 0x80105A10u, "D8 BLNE F898     (VA)");
            Hook(tm, 0x80105A14u, "D8 LDR[r4+0xC]   (VA)");
            /* Exception vectors (SCTLR.V=1 -> high vectors 0xFFFF00xx). If the
               VA 0x20 LDR faults, the data-abort vector 0xFFFF0010 fires; a
               recursive prefetch-abort would show 0xFFFF000C too. */
            Hook(tm, 0xFFFF0004u, "VEC undef        (VA)");
            Hook(tm, 0xFFFF0008u, "VEC svc          (VA)");
            Hook(tm, 0xFFFF000Cu, "VEC pf-abort     (VA)");
            Hook(tm, 0xFFFF0010u, "VEC data-abort   (VA)");
            Hook(tm, 0xFFFF0018u, "VEC irq          (VA)");
            Hook(tm, 0x00000010u, "VEC lo data-abort(VA)");
            /* sub_80105A54 module-walk: why kernel-entry (v44) computes to 0. */
            HookTarget(tm, 0x80105A54u, "A54-entry");
            HookA54(tm, 0x80105A70u, "A54-loop r4=toc r6=idx");
            HookA54(tm, 0x80105AA4u, "A54-MATCH r4=toc");
            HookA54(tm, 0x80105AB0u, "A54-e32 r3=e32ptr");
            HookA54(tm, 0x80105AB8u, "A54-entrycalc r0=v44");
            HookA54(tm, 0x80105A98u, "A54-NOMATCH ret0");
        });
    }

private:
    void HookPteDump(TraceManager& tm, uint32_t va, const char* tag) {
        tm.OnPc(va, [this, va, tag](const TraceContext& c) {
            int& n = fires_[va];
            if (n >= 4) return;
            ++n;
            /* TTBR0 = R10 = 0x90DB0000 (PA), mapped at VA 0x80DB0000. The L1
               descriptor for VA v is at TTBR0 + (v>>20)*4; VA 0x20 -> L1[0]. */
            auto rd = [&c](uint32_t a) {
                auto v = c.ReadVa32(a);
                return v ? *v : 0xDEADBEEFu;
            };
            LOG(Jit, "[PARK] %s pc=0x%08X r7=0x%08X | L1[0]=0x%08X "
                "peek(VA0x20)=0x%08X\n",
                tag, c.pc, c.regs[7], rd(0x80DB0000u), rd(0x20u));
        });
    }

    /* Read up to 15 chars of a guest string into buf (NUL-terminated). */
    static void ReadStr(const TraceContext& c, uint32_t va, char* buf) {
        int i = 0;
        for (; i < 15; ++i) {
            auto ch = c.ReadVa8(va + static_cast<uint32_t>(i));
            if (!ch || *ch == 0) break;
            buf[i] = (*ch >= 0x20 && *ch < 0x7f) ? static_cast<char>(*ch) : '?';
        }
        buf[i] = 0;
    }

    void HookTarget(TraceManager& tm, uint32_t va, const char* tag) {
        tm.OnPc(va, [this, va, tag](const TraceContext& c) {
            int& n = fires_[va];
            if (n >= 3) return;
            ++n;
            const uint32_t ptoc = c.regs[0];
            char target[16]{}, mod0[16]{};
            ReadStr(c, 0x80101968u, target);
            /* first TOCentry name ptr at pTOC+0x54+0x10 = pTOC+0x64 */
            auto namep = c.ReadVa32(ptoc + 0x64u);
            if (namep) ReadStr(c, *namep, mod0);
            LOG(Jit, "[A54] %s pTOC=0x%08X target@0x80101968='%s' "
                "[0x80101968]=0x%08X mod0name@0x%08X='%s'\n",
                tag, ptoc, target,
                c.ReadVa32(0x80101968u) ? *c.ReadVa32(0x80101968u) : 0xDEADBEEFu,
                namep ? *namep : 0u, mod0);
        });
    }

    void HookA54(TraceManager& tm, uint32_t va, const char* tag) {
        tm.OnPc(va, [this, va, tag](const TraceContext& c) {
            int& n = fires_[va];
            if (n >= 6) return;
            ++n;
            LOG(Jit, "[A54] %s pc=0x%08X r0=0x%08X r2=0x%08X r3=0x%08X "
                "r4=0x%08X r5=0x%08X r6=0x%08X\n",
                tag, c.pc, c.regs[0], c.regs[2], c.regs[3],
                c.regs[4], c.regs[5], c.regs[6]);
        });
    }

    void Hook(TraceManager& tm, uint32_t va, const char* tag) {
        tm.OnPc(va, [this, va, tag](const TraceContext& c) {
            int& n = fires_[va];
            if (n >= 8) return;  /* cap: a park-loop would otherwise flood */
            ++n;
            LOG(Jit, "[PARK] %s pc=0x%08X r0=0x%08X r4=0x%08X r5=0x%08X "
                "r7=0x%08X r10=0x%08X r12=0x%08X lr=0x%08X sp=0x%08X\n",
                tag, c.pc, c.regs[0], c.regs[4], c.regs[5], c.regs[7],
                c.regs[10], c.regs[12], c.regs[14], c.regs[13]);
        });
    }

    std::unordered_map<uint32_t, int> fires_;
};

}  /* namespace */

REGISTER_SERVICE(Sync2KernelParkTrace);

#endif  /* CERF_DEV_MODE */
