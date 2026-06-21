#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../jit/arm_mmu.h"
#include "../../jit/arm_mmu_state.h"
#include "bundle.h"
#include "zune_process_resolver.h"

#include <atomic>
#include <cstdint>

#if CERF_DEV_MODE

namespace {

/* gemstone render-init = XuiRenderInitExclusive (xuidll 0x34D41D8) -> ... -> D3DM
   init sub_34E8CDC, which at 0x34E8ED0 requires the display mode be 16bpp RGB565
   (bpp 0x10, masks 0xF800/0x7E0/0x1F) else E_FAIL -> relaunch. Hook logs the
   format D3DM reads from our HAL. */
class ZuneXuiRenderInitBisect : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kZuneKeelBundleCrc32, [&] {
            auto gem = zune_resolver::PidPredicateForName("gemstone.exe");

            tm.OnPcFiltered(0x34D41D8u, gem, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 8) return;
                LOG(Trace, "[XUI-INIT] XuiRenderInitExclusive ENTRY lr=%08X\n",
                    c.regs[14]);
            });
            /* 0x34E8ED0: R3 = display-mode bpp (just LDR'd, about to CMP #0x10);
               masks at [SP+0x18]/[SP+0x1C]/[SP+0x20]. Logs what D3DM reads. */
            tm.OnPcFiltered(0x34E8ED0u, gem, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 8) return;
                const uint32_t sp = c.regs[13];
                LOG(Trace, "[XUI-INIT] D3DM mode-format check: bpp=%u R=%08X G=%08X "
                           "B=%08X (needs 16/F800/7E0/1F)\n",
                    c.regs[3],
                    c.ReadVa32(sp + 0x18u).value_or(0xDEADBEEFu),
                    c.ReadVa32(sp + 0x1Cu).value_or(0xDEADBEEFu),
                    c.ReadVa32(sp + 0x20u).value_or(0xDEADBEEFu));
            });
            /* D3DM span-fill selector sub_35000A4 return (0x35004C0 POP {...,PC}):
               R0 = the chosen fill routine. The last one logged before GEM-XEXIT is
               the routine the rasterizer crashed inside (PC->0x18811881). */
            tm.OnPcFiltered(0x35004C0u, gem, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 400) return;
                /* epilogue POP {R4-R11,PC}: return addr (caller of the selector,
                   which then BLX's the returned routine) = [sp + 0x20]. */
                LOG(Trace, "[XUI-FILL] selector#%u -> fill routine=0x%08X retTo=0x%08X\n",
                    n.load(), c.regs[0],
                    c.ReadVa32(c.regs[13] + 0x20u).value_or(0xDEADBEEFu));
            });
            tm.OnPcFiltered(0x34E99F4u, gem, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 8) return;
                LOG(Trace, "[XUI-SPAN] sub_34E99F4 ENTERED a1=%08X\n", c.regs[0]);
            });
            /* sub_34EA324 stores the selector result at a1[2950] (the fn ptr that
               later crashes to 0x18811881); a1=r0 = D3DM render ctx. */
            tm.OnPcFiltered(0x34EA324u, gem, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 6) return;
                const uint32_t a1 = c.regs[0];
                auto rd = [&](uint32_t idx) {
                    return c.ReadVa32(a1 + idx * 4u).value_or(0xDEADBEEFu);
                };
                LOG(Trace, "[XUI-CTX] a1=%08X dst[2932]=%08X stride[2912]=%u "
                           "fnptr[2948..52]=%08X %08X %08X %08X %08X\n",
                    a1, rd(2932), rd(2912),
                    rd(2948), rd(2949), rd(2950), rd(2951), rd(2952));
            });
            /* The post-boot scene wedges re-applying one property forever
               (XuiObjectSetProperty 0x349C2EC -> sub_349C018 -> XuiSoundSetFile,
               identical args, ~8000x, frame never Present'd). Dump the return
               stack mid-spin (call #3000) to name the loop owner above the setter. */
            tm.OnPcFiltered(0x349C2ECu, gem, [](const TraceContext& c) {
                /* Isolate the spin from normal (varied) property sets: dump only
                   after 50 consecutive calls with the same handle+propid. */
                static uint32_t last_h = 0, last_p = 0, run = 0;
                static std::atomic<uint32_t> dumped{0};
                if (c.regs[0] == last_h && c.regs[1] == last_p) ++run;
                else { run = 0; last_h = c.regs[0]; last_p = c.regs[1]; }
                if (run != 50u || dumped.fetch_add(1) != 0u) return;
                const uint32_t sp = c.regs[13];
                LOG(Trace, "[XUI-SPIN] XuiObjectSetProperty h=%08X p=%08X lr=%08X sp=%08X "
                           "stk: %08X %08X %08X %08X %08X %08X %08X %08X "
                           "%08X %08X %08X %08X %08X %08X %08X %08X\n",
                    c.regs[0], c.regs[1], c.regs[14], sp,
                    c.ReadVa32(sp + 0x00u).value_or(0u), c.ReadVa32(sp + 0x04u).value_or(0u),
                    c.ReadVa32(sp + 0x08u).value_or(0u), c.ReadVa32(sp + 0x0Cu).value_or(0u),
                    c.ReadVa32(sp + 0x10u).value_or(0u), c.ReadVa32(sp + 0x14u).value_or(0u),
                    c.ReadVa32(sp + 0x18u).value_or(0u), c.ReadVa32(sp + 0x1Cu).value_or(0u),
                    c.ReadVa32(sp + 0x20u).value_or(0u), c.ReadVa32(sp + 0x24u).value_or(0u),
                    c.ReadVa32(sp + 0x28u).value_or(0u), c.ReadVa32(sp + 0x2Cu).value_or(0u),
                    c.ReadVa32(sp + 0x30u).value_or(0u), c.ReadVa32(sp + 0x34u).value_or(0u),
                    c.ReadVa32(sp + 0x38u).value_or(0u), c.ReadVa32(sp + 0x3Cu).value_or(0u));
            });
            /* XuiRenderBegin wedge error code: 0x34D55B8 = R0 after the D3DM
               vtable+0x14 call; 0x34D55DC = R0 after sub_34D09DC. Logged only when
               negative (the failing result that skips End/Present). */
            tm.OnPcFiltered(0x34D55B8u, gem, [](const TraceContext& c) {
                if ((int32_t)c.regs[0] >= 0) return;
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 8) return;
                LOG(Trace, "[RB-FAIL] vtable+0x14 ret=%08X\n", c.regs[0]);
            });
            tm.OnPcFiltered(0x34D55DCu, gem, [](const TraceContext& c) {
                if ((int32_t)c.regs[0] >= 0) return;
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 8) return;
                LOG(Trace, "[RB-FAIL] sub_34D09DC ret=%08X\n", c.regs[0]);
            });
            /* sub_1C52C = the continuous shell pump (XuiRenderBeginEx->draw->Present)
               that stops at the wedge. Entry heartbeat (+lr=driver) shows if the pump
               stops being called; 0x1C540 logs XuiRenderBeginEx only when it fails. */
            tm.OnPcFiltered(0x1C52Cu, gem, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                const uint32_t i = n.fetch_add(1);
                if (i < 3u || i % 256u == 0u)
                    LOG(Trace, "[RPUMP] sub_1C52C entry #%u lr=%08X\n", i, c.regs[14]);
            });
            tm.OnPcFiltered(0x1C540u, gem, [](const TraceContext& c) {
                if ((int32_t)c.regs[0] >= 0) return;
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 8) return;
                LOG(Trace, "[RPUMP] XuiRenderBeginEx FAIL ret=%08X\n", c.regs[0]);
            });
            /* gemstone's render gate = GetDevicePower(Display, type R5==3) <= D2
               (sub_6B8DC). pdwState[0] is at SP+0 after the call (0x6B994). Log the
               Display power state + timing to confirm the D0->D3 transition that
               closes the gate and pin when it happens. */
            tm.OnPcFiltered(0x6B994u, gem, [](const TraceContext& c) {
                if (c.regs[5] != 3u) return;
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 80u) return;
                LOG(Trace, "[DPOWER] Display GetDevicePower hr=%08X state=%d\n",
                    c.regs[0], (int)c.ReadVa32(c.regs[13]).value_or(0xFFFFFFFFu));
            });
            /* First USER-range fault (bankedLR not PSL 0xFxxxxxxx, not kernel
               0x8xxxxxxx) = the crash; the steady-state faults are all PSL/kernel.
               r0..r12 are the faulting context's GPRs (r13/r14 banked to abort). */
            {
                struct Vec { uint32_t ea; const char* nm; };
                static const Vec kV[] = {
                    {0xFFFF0004u, "UNDEF"}, {0xFFFF000Cu, "PFA"}, {0xFFFF0010u, "DABT"},
                };
                auto cnt = std::make_shared<std::atomic<uint32_t>>(0);
                for (const auto& v : kV) {
                    const char* nm = v.nm;
                    tm.OnPcFiltered(v.ea, gem, [nm, cnt](const TraceContext& c) {
                        const uint32_t lr = c.regs[14];
                        /* Only the crash carries the 0x1881xxxx pixel-garbage in
                           a reg; skip the normal demand-paging user faults. */
                        bool hit = (lr & 0xFFFF0000u) == 0x18810000u;
                        for (int i = 0; i < 13 && !hit; ++i)
                            hit = (c.regs[i] & 0xFFFF0000u) == 0x18810000u;
                        if (!hit) return;
                        if (cnt->fetch_add(1) >= 4) return;
                        const auto* ms = c.emu.Get<ArmMmu>().State();
                        LOG(Trace, "[XUI-FAULTVA] fault_address=%08X status=%08X "
                                   "wnr=%u\n",
                            ms->fault_address, ms->fault_status.word,
                            ms->fault_status.bits.wnr);
                        /* Walk the L1 descriptor for the surface VA under each
                           candidate FCSE fold (raw / gwes slot8 / gemstone slotC)
                           to locate where the Lock's VirtualCopy installed the PTE. */
                        auto& mem = c.emu.Get<EmulatedMemory>();
                        const uint32_t ttbr =
                            ms->translation_table_base.word & 0xFFFFC000u;
                        auto l1 = [&](uint32_t va) -> uint32_t {
                            const uint8_t* h =
                                mem.TryTranslate(ttbr + ((va >> 20) << 2));
                            return h ? *reinterpret_cast<const uint32_t*>(h)
                                     : 0xDEADBEEFu;
                        };
                        const uint32_t l1c = l1(0x0C280950u);
                        LOG(Trace, "[XUI-PTE] ttbr=%08X procid=%08X L1[00280950]=%08X "
                                   "L1[08280950]=%08X L1[0C280950]=%08X\n",
                            ttbr, ms->process_id, l1(0x00280950u),
                            l1(0x08280950u), l1c);
                        if ((l1c & 3u) == 1u) {
                            const uint32_t l2base = l1c & 0xFFFFFC00u;
                            auto l2 = [&](uint32_t va) -> uint32_t {
                                const uint32_t idx = (va >> 12) & 0xFFu;
                                const uint8_t* h =
                                    mem.TryTranslate(l2base + idx * 4u);
                                return h ? *reinterpret_cast<const uint32_t*>(h)
                                         : 0xDEADBEEFu;
                            };
                            for (int k = -2; k <= 5; ++k) {
                                const uint32_t va = 0x0C280000u + (uint32_t)(k << 12);
                                LOG(Trace, "[XUI-L2] page=%08X L2=%08X\n", va, l2(va));
                            }
                        }
                        LOG(Trace, "[XUI-CRASH] %s bankedLR=%08X bankedSP=%08X "
                                   "r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X "
                                   "r6=%08X r7=%08X r8=%08X r9=%08X r10=%08X r11=%08X "
                                   "r12=%08X\n",
                            nm, lr, c.regs[13], c.regs[0], c.regs[1], c.regs[2],
                            c.regs[3], c.regs[4], c.regs[5], c.regs[6], c.regs[7],
                            c.regs[8], c.regs[9], c.regs[10], c.regs[11], c.regs[12]);
                    });
                }
            }
            /* The faulting fill store (PC 0x03FC5B30, a RAM DLL not in IDA),
               gated to the crashing invocation (0x1881 fill value in a reg) - the
               routine is a generic fill called many times. LR = the caller (likely
               a module that IS in IDA) → names the next frame up. */
            tm.OnPcFiltered(0x03FC5B30u, gem, [](const TraceContext& c) {
                bool hit = false;
                for (int i = 0; i < 13 && !hit; ++i)
                    hit = (c.regs[i] & 0xFFFF0000u) == 0x18810000u;
                if (!hit) return;
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 3) return;
                const uint32_t sp = c.regs[13];
                LOG(Trace, "[XUI-FILLPC] lr=%08X r0=%08X r2=%08X r6=%08X r10=%08X "
                           "r11=%08X r12=%08X sp=%08X [sp]=%08X [sp+4]=%08X "
                           "[sp+8]=%08X [sp+C]=%08X\n",
                    c.regs[14], c.regs[0], c.regs[2], c.regs[6], c.regs[10],
                    c.regs[11], c.regs[12], sp,
                    c.ReadVa32(sp).value_or(0xDEADBEEFu),
                    c.ReadVa32(sp + 4u).value_or(0xDEADBEEFu),
                    c.ReadVa32(sp + 8u).value_or(0xDEADBEEFu),
                    c.ReadVa32(sp + 0xCu).value_or(0xDEADBEEFu));
            });
            /* sub_34E9C7C computes the block-copy extent: rows = clamp(a5,clip) -
               clamp(a4,clip); memcpy size = rows*stride. dst=a1[2932], clip top/
               height = a1[2915]/a1[2917], stride = a1[2912]. Filtered to the
               crashing 0x280950 surface; the overrunning call has clipBottom>600. */
            tm.OnPcFiltered(0x34E9C7Cu, gem, [](const TraceContext& c) {
                const uint32_t a1 = c.regs[0];
                auto rd = [&](uint32_t idx) {
                    return c.ReadVa32(a1 + idx * 4u).value_or(0xDEADBEEFu);
                };
                if (rd(2932) != 0x00280950u) return;
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 8) return;
                const uint32_t sp = c.regs[13];
                LOG(Trace, "[XUI-SPANARG] a2=%d a3=%d a4(top)=%d a5(bot)=%d a8=%08X "
                           "stride[2912]=%u clipX[2914]=%d clipT[2915]=%d "
                           "clipW[2916]=%d clipH[2917]=%d -> clipBottom=%d\n",
                    (int)c.regs[1], (int)c.regs[2], (int)c.regs[3],
                    (int)c.ReadVa32(sp).value_or(0), c.ReadVa32(sp + 0xCu).value_or(0),
                    rd(2912), (int)rd(2914), (int)rd(2915), (int)rd(2916),
                    (int)rd(2917), (int)(rd(2915) + rd(2917)));
            });

            /* ddraw Flip thin client sub_37AD9C0 (surface vtable+0x2C): it only
               traps MEMORY[0x1F6C1A4] (F00082D4) to the ddcore server and returns
               the result. 0x37ADA1C = pre-trap (R0=serverHandle=*(this-8),
               R1=override, R2=flags); 0x37ADA24 = post-trap (R0=result). */
            const auto any = [](const TraceContext&) -> bool { return true; };
            tm.OnPcFiltered(0x37ADA1Cu, gem, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 12) return;
                LOG(Trace, "[FLIP-CALL] serverHandle=%08X override=%08X flags=%08X "
                           "pid=%08X\n",
                    c.regs[0], c.regs[1], c.regs[2],
                    c.emu.Get<ArmMmu>().State()->process_id);
            });
            tm.OnPcFiltered(0x37ADA24u, gem, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 12) return;
                LOG(Trace, "[FLIP-RET] result=%08X pid=%08X\n",
                    c.regs[0], c.emu.Get<ArmMmu>().State()->process_id);
            });
            /* ddcore flip-op candidate sub_376ABF4 + Blt server sub_376BE70
               (control). 376ABF4 firing between [FLIP-CALL] and [FLIP-RET] = the
               trap reached ddcore (rejection is ddcore validation); no ddcore
               flip-op = the kernel api-set layer rejected pre-ddcore. */
            tm.OnPcFiltered(0x376ABF4u, any, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 16) return;
                LOG(Trace, "[DDC-FLIPOP] sub_376ABF4 r0=%08X r1=%08X pid=%08X "
                           "lr=%08X\n",
                    c.regs[0], c.regs[1],
                    c.emu.Get<ArmMmu>().State()->process_id, c.regs[14]);
            });
            /* sub_376BE70 returns DDERR_EXCEPTION (0x88760037) via its SEH after a
               fault in a non-ddcore callee. Arm one GWES fault-capture per blt
               entry so the recurring fault_address names the bad pointer. */
            auto bltGen = std::make_shared<std::atomic<uint32_t>>(0);
            auto fltGen = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPcFiltered(0x376BE70u, any, [bltGen](const TraceContext& c) {
                if (c.emu.Get<ArmMmu>().State()->process_id == 0x08000000u)
                    bltGen->fetch_add(1);
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 8) return;
                LOG(Trace, "[DDC-BLT] sub_376BE70 pid=%08X lr=%08X\n",
                    c.emu.Get<ArmMmu>().State()->process_id, c.regs[14]);
            });
            tm.OnPcFiltered(0xFFFF0010u,
                [bltGen, fltGen](const TraceContext& c) {
                    return c.emu.Get<ArmMmu>().State()->process_id == 0x08000000u
                        && bltGen->load() != fltGen->load();
                },
                [bltGen, fltGen](const TraceContext& c) {
                    fltGen->store(bltGen->load());
                    const auto* ms = c.emu.Get<ArmMmu>().State();
                    LOG(Trace, "[BLT-FLT] far=%08X status=%08X bankedLR=%08X "
                               "r0=%08X r1=%08X r2=%08X r3=%08X\n",
                        ms->fault_address, ms->fault_status.word, c.regs[14],
                        c.regs[0], c.regs[1], c.regs[2], c.regs[3]);
                });

            /* 0x377CA68 = the verified 0x80070057 mem-class-mismatch reject in ddcore's
               Flip server (sub_377C594). Keep `any`-filtered, NOT gemstone: the Flip
               server runs in the GWES server process, so a client filter would never fire. */
            tm.OnPcFiltered(0x377C9B8u, any, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 12) return;
                LOG(Trace, "[FLIP-CAPS] backbufCaps=%08X primaryCaps=%08X pid=%08X\n",
                    c.regs[2], c.ReadVa32(c.regs[8] + 0x20u).value_or(0xDEADBEEFu),
                    c.emu.Get<ArmMmu>().State()->process_id);
            });
            tm.OnPcFiltered(0x377CA68u, any, [](const TraceContext& c) {
                static std::atomic<uint32_t> n{0};
                if (n.fetch_add(1) >= 12) return;
                LOG(Trace, "[FLIP-REJECT] mem-class mismatch -> 0x80070057 lr=%08X pid=%08X\n",
                    c.regs[14], c.emu.Get<ArmMmu>().State()->process_id);
            });
        });
    }
};

REGISTER_SERVICE(ZuneXuiRenderInitBisect);

}  // namespace

#endif  // CERF_DEV_MODE
