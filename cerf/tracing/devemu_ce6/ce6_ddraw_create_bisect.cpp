#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../jit/arm/arm_mmu_state.h"
#include "devemu_ce6_bundle.h"

#if CERF_DEV_MODE

#include <atomic>
#include <cstdint>

namespace {

/* Bisects the CE6 GA DirectDrawCreate=0x8007000E (E_OUTOFMEMORY): hooks the OOM
   sites in the gwes.dll DD-HAL loader sub_C01B5660 (exec VA == IDA VA). */
class Ce6DdrawCreateBisect : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kDevemuCe6BundleCrc32, [&tm] {
            auto cap = [](std::shared_ptr<std::atomic<uint32_t>> n, uint32_t max) {
                return n->fetch_add(1) >= max;
            };

            /* Loader entry (sub_C01B5660): a1=R0 = driver name wide ptr. */
            auto e = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B5660u, [e, cap](const TraceContext& c) {
                if (cap(e, 8)) return;
                LOG(Trace, "[CE6DD] loader sub_C01B5660 ENTRY a1(name)=%08X lr=%08X\n",
                    c.regs[0], c.regs[14]);
            });

            /* dwSize gate (0xC01B5798 CMP R3,#0x11C): R3 = DDHALINFO.dwSize. */
            auto d = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B5798u, [d, cap](const TraceContext& c) {
                if (cap(d, 8)) return;
                LOG(Trace, "[CE6DD] dwSize check R3=%u (expect 284)\n", c.regs[3]);
            });

            /* Mode-record count (0xC01B5858 MOVS R2,R3 after LDR R3,[R5,#0x2E4]).
               Nonzero -> new[](20*count); zero -> skipped. */
            auto m = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B5858u, [m, cap](const TraceContext& c) {
                if (cap(m, 8)) return;
                LOG(Trace, "[CE6DD] mode-record count [R5+0x2E4]=R3=%u\n", c.regs[3]);
            });
            /* mode-record new[] result (0xC01B587C CMP R0,#0) - only if count!=0. */
            auto ma = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B587Cu, [ma, cap](const TraceContext& c) {
                if (cap(ma, 8)) return;
                LOG(Trace, "[CE6DD] mode new[] R0=%08X %s\n",
                    c.regs[0], c.regs[0] ? "ok" : "OOM(site1)");
            });

            /* sub_C01B51F8 result (0xC01B5958 CMP R0,#0): 0 -> LABEL_44 E_FAIL. */
            auto s1 = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B5958u, [s1, cap](const TraceContext& c) {
                if (cap(s1, 8)) return;
                LOG(Trace, "[CE6DD] sub_C01B51F8 R0=%08X %s\n",
                    c.regs[0], c.regs[0] ? "ok" : "ZERO->E_FAIL");
            });

            /* new[](0x80) result (0xC01B598C CMP R0,#0). */
            auto a80 = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B598Cu, [a80, cap](const TraceContext& c) {
                if (cap(a80, 8)) return;
                LOG(Trace, "[CE6DD] new[](0x80) R0=%08X %s\n",
                    c.regs[0], c.regs[0] ? "ok" : "OOM(site2)");
            });
            /* new[](0x200) result (0xC01B59AC CMP R0,#0). */
            auto a200 = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B59ACu, [a200, cap](const TraceContext& c) {
                if (cap(a200, 8)) return;
                LOG(Trace, "[CE6DD] new[](0x200) R0=%08X %s\n",
                    c.regs[0], c.regs[0] ? "ok" : "OOM(site3)");
            });

            /* sub_C01B538C result (0xC01B5A28 CMP R0,#0): 0 -> OOM (prime suspect). */
            auto s38c = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B5A28u, [s38c, cap](const TraceContext& c) {
                if (cap(s38c, 8)) return;
                LOG(Trace, "[CE6DD] sub_C01B538C R0=%08X %s\n",
                    c.regs[0], c.regs[0] ? "ok" : "OOM(site4)");
            });
            /* sub_C01B05E0 result (0xC01B5A40 CMP R0,#0): 0 -> OOM (prime suspect). */
            auto s5e0 = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B5A40u, [s5e0, cap](const TraceContext& c) {
                if (cap(s5e0, 8)) return;
                LOG(Trace, "[CE6DD] sub_C01B05E0 R0=%08X %s\n",
                    c.regs[0], c.regs[0] ? "ok" : "OOM(site5)");
            });

            /* VirtualAllocCopyEx call site (0xC01B0248 BL, R2=base R3=size): walk
               the gwes source page tables across [base, base+size] to see how much
               of the advertised vidmem is actually mapped as the copy source. */
            auto vac = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B0248u, [vac, cap](const TraceContext& c) {
                if (cap(vac, 4)) return;
                const uint32_t base = c.regs[2], size = c.regs[3];
                auto& mem = c.emu.Get<EmulatedMemory>();
                const auto* ms = c.emu.Get<ArmMmu>().State();
                const uint32_t ttbr = ms->translation_table_base.word & 0xFFFFC000u;
                auto rd = [&](uint32_t pa) -> uint32_t {
                    const uint8_t* h = mem.TryTranslate(pa);
                    return h ? *reinterpret_cast<const uint32_t*>(h) : 0xDEADBEEFu;
                };
                auto mapped = [&](uint32_t va) -> int {
                    const uint32_t l1 = rd(ttbr + ((va >> 20) << 2));
                    if ((l1 & 3u) == 2u) return 1;                 /* section */
                    if ((l1 & 3u) == 1u) {                          /* coarse -> L2 */
                        const uint32_t l2 =
                            rd((l1 & 0xFFFFFC00u) + (((va >> 12) & 0xFFu) << 2));
                        return (l2 & 3u) != 0u ? 1 : 0;
                    }
                    return 0;
                };
                LOG(Trace, "[CE6DD] VirtualAllocCopyEx base=%08X size=%08X dstPid=%08X "
                           "mapped@[+0]=%d [+1M]=%d [+8M]=%d [+16M]=%d [+end]=%d\n",
                    base, size, c.regs[1],
                    mapped(base), mapped(base + 0x100000u), mapped(base + 0x800000u),
                    mapped(base + 0x1000000u), mapped(base + size - 0x1000u));
            });

            /* Final error/return decision (0xC01B5A58 CMP R4,#0; BPL success):
               R4 = HRESULT being returned (>=0 success, <0 error). */
            auto fin = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0xC01B5A58u, [fin, cap](const TraceContext& c) {
                if (cap(fin, 8)) return;
                LOG(Trace, "[CE6DD] loader return R4(hr)=%08X\n", c.regs[4]);
            });
        });
    }
};

REGISTER_SERVICE(Ce6DdrawCreateBisect);

}  /* namespace */

#endif  /* CERF_DEV_MODE */
