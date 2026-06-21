#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../jit/arm_mmu.h"
#include "../../jit/arm_mmu_state.h"
#include "bundle.h"

#include <atomic>
#include <cstdint>
#include <memory>

#if CERF_DEV_MODE

namespace {

/* UNFILTERED is required: these ddcore VAs are above the 32MB FCSE fold (shared XIP-DLL
   region, same physical code per process), and the CreateSurface server runs in the
   GWES/device.exe owner process, never in ddrawtest - a ddrawtest-pid filter never fires. */
class DevEmuCe5DdrawCreateBisect : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kDevEmuCe5BundleCrc32, [&tm] {
            /* CS-LOADER (ddrawtest's DirectDrawCreate) sets this; CS-PSL only logs after,
               so the boot PSL flood in the reused 0x0A000000 slot can't exhaust the cap
               before ddrawtest's CreateSurface window. */
            auto armed = std::make_shared<std::atomic<bool>>(false);
            auto entryN = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0x24D8B24u, [entryN](const TraceContext& c) {
                if (entryN->fetch_add(1) >= 32) return;
                const uint32_t a5 = c.ReadVa32(c.regs[13]).value_or(0xDEADBEEFu);
                LOG(Trace, "[CS-SRV] sub_24D8B24 ENTRY a1=%08X a2(caps)=%08X a3=%08X "
                           "a4(out)=%08X a5=%08X lr=%08X pid=%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3], a5, c.regs[14],
                    c.emu.Get<ArmMmu>().State()->process_id);
            });

            /* Prefetch-abort vector: a PSL implicit-call (0xF00xxxxx) faults here, abort-mode
               LR = trap+4. A 0xF000xxxx trap for CreateSurface => it traps to the kernel (so
               the kernel marshal rejects, since no ddcore stub fired); none => ddraw rejects
               client-side. Pid-filtered so boot PSL traffic returns on one compare. */
            auto ddrawtestPid = [](const TraceContext& c) {
                return c.emu.Get<ArmMmu>().State()->process_id == 0x0A000000u;
            };
            auto pslN = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPcFiltered(0xFFFF000Cu, ddrawtestPid, [pslN, armed](const TraceContext& c) {
                if (!armed->load()) return;
                const uint32_t trap = c.regs[14] - 4u;
                if ((trap & 0xFFFF0000u) != 0xF0000000u) return;
                if (pslN->fetch_add(1) >= 80) return;
                LOG(Trace, "[CS-PSL] trap=%08X r0=%08X r1=%08X r2=%08X r3=%08X\n",
                    trap, c.regs[0], c.regs[1], c.regs[2], c.regs[3]);
            });

            /* ddraw client CreateSurface = sub_250470C -> sub_2504B90 ->
               MEMORY[0x1E5613C](obj[1],desc,&out), the path returning 0x80070057. ddraw
               .text is at 0x250xxxx (shared region >32MB, hookable at IDA VA like ddcore);
               the dispatch ptr reads folded since ddraw's .data is live here. */
            auto cliN = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPcFiltered(0x250470Cu, ddrawtestPid, [cliN](const TraceContext& c) {
                if (cliN->fetch_add(1) >= 8) return;
                LOG(Trace, "[CS-CLI] sub_250470C this=%08X desc=%08X lplpSurf=%08X\n",
                    c.regs[0], c.regs[1], c.regs[2]);
            });
            auto b90N = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPcFiltered(0x2504B90u, ddrawtestPid, [b90N](const TraceContext& c) {
                if (b90N->fetch_add(1) >= 8) return;
                const uint32_t pid = c.emu.Get<ArmMmu>().State()->process_id;
                const uint32_t disp =
                    c.ReadVa32((0x01E5613Cu & 0x01FFFFFFu) | pid).value_or(0xDEADBEEFu);
                LOG(Trace, "[CS-CLI] sub_2504B90 this=%08X a2=%08X desc=%08X out=%08X "
                           "dispatch[1E5613C]=%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3], disp);
            });

            struct Rej { uint32_t ea; const char* nm; };
            static const Rej kRej[] = {
                {0x24D8BCCu, "a4==NULL -> 0x80070057"},
                {0x24D8C10u, "caps&0xFFFFF800 -> 0x80070057 (r1=raw desc ptr?)"},
                {0x24D8C48u, "no-exclusive -> 0x887600D4"},
                {0x24D8C7Cu, "caps&0xB0 -> 0x80070057"},
                {0x24D8CE8u, "exclusivity v13!=1 -> 0x80070057 (caps wrong/0)"},
            };
            for (const auto& r : kRej) {
                const char* nm = r.nm;
                auto cnt = std::make_shared<std::atomic<uint32_t>>(0);
                tm.OnPc(r.ea, [nm, cnt](const TraceContext& c) {
                    if (cnt->fetch_add(1) >= 16) return;
                    LOG(Trace, "[CS-REJ] %s  caps(R6)=%08X lr=%08X pid=%08X\n",
                        nm, c.regs[6], c.regs[14],
                        c.emu.Get<ArmMmu>().State()->process_id);
                });
            }

            /* sub_24F2AC4 = "DDRW" api-set method-5 stub (kernel dispatches the trap here,
               it tail-calls *(P+0x18) -> sub_24D8B24). Fires => stub/target rejects; silent
               => kernel marshal rejected per the sig first. sub_24F29E8 = AddRef control. */
            auto m5N = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0x24F2AC4u, [m5N](const TraceContext& c) {
                if (m5N->fetch_add(1) >= 16) return;
                const uint32_t a1 = c.regs[0];
                const uint32_t P = c.ReadVa32(a1).value_or(0xDEADBEEFu);
                const uint32_t tgt = c.ReadVa32(P + 0x18u).value_or(0xDEADBEEFu);
                LOG(Trace, "[CS-STUB5] sub_24F2AC4 a1=%08X P=%08X tgt(P+0x18)=%08X "
                           "r1(desc)=%08X r2=%08X r3=%08X lr=%08X pid=%08X\n",
                    a1, P, tgt, c.regs[1], c.regs[2], c.regs[3], c.regs[14],
                    c.emu.Get<ArmMmu>().State()->process_id);
            });
            auto arN = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0x24F29E8u, [arN](const TraceContext& c) {
                if (arN->fetch_add(1) >= 6) return;
                LOG(Trace, "[CS-ADDREF] sub_24F29E8 a1=%08X lr=%08X pid=%08X\n",
                    c.regs[0], c.regs[14], c.emu.Get<ArmMmu>().State()->process_id);
            });

            /* Proven-firing reference: the ddcore DirectDrawCreate loader runs HALInit
               in ddrawtest's slot during DirectDrawCreate. */
            auto ldN = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0x24C5274u, [ldN, armed](const TraceContext& c) {
                armed->store(true);
                if (ldN->fetch_add(1) >= 4) return;
                /* MEMORY[0x1E5613C] = the runtime CreateSurface dispatch ptr (sub_2504B90
                   tail-calls it), in ddraw's <32MB FCSE-folded .data which isn't GuestTlb
                   resident at this ddcore-context PC, so a fast-path peek misses; full L1/L2
                   walk through the current TTBR for the folded VA reads it. */
                const auto* ms = c.emu.Get<ArmMmu>().State();
                const uint32_t pid = ms->process_id;
                auto& mem = c.emu.Get<EmulatedMemory>();
                const uint32_t ttbr = ms->translation_table_base.word & 0xFFFFC000u;
                auto rd = [&](uint32_t pa) -> uint32_t {
                    const uint8_t* h = mem.TryTranslate(pa);
                    return h ? *reinterpret_cast<const uint32_t*>(h) : 0xDEADBEEFu;
                };
                auto walk = [&](uint32_t va) -> uint32_t {
                    const uint32_t fv = (va & 0x01FFFFFFu) | pid;
                    const uint32_t l1 = rd(ttbr + ((fv >> 20) << 2));
                    uint32_t pa;
                    if ((l1 & 3u) == 2u) pa = (l1 & 0xFFF00000u) | (fv & 0x000FFFFFu);
                    else if ((l1 & 3u) == 1u)
                        pa = (rd((l1 & 0xFFFFFC00u) + (((fv >> 12) & 0xFFu) << 2)) & 0xFFFFF000u)
                           | (fv & 0xFFFu);
                    else return 0xBAD00000u | (l1 & 0xFFu);
                    return rd(pa);
                };
                LOG(Trace, "[CS-LOADER] sub_24C5274 lr=%08X pid=%08X [1E5613C]=%08X "
                           "[1E561F4]=%08X\n",
                    c.regs[14], pid, walk(0x01E5613Cu), walk(0x01E561F4u));
            });
        });
    }
};

REGISTER_SERVICE(DevEmuCe5DdrawCreateBisect);

}  // namespace

#endif  // CERF_DEV_MODE
