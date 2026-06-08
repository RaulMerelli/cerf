#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace {

/* Boot-chain probe for jLime's jlinexec.exe (CF-card Linux bootloader).
   Hook VAs and instruction-word signatures are jlinexec's IDA bytes. */
class JlinexecBootProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            auto& tm = emu_.Get<TraceManager>();

            Hook(tm, 0x11AD0u, 0xE92D4000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] WinMain entry (launch #%u) "
                           "lr=0x%08X sp=0x%08X cpsr=0x%08X\n",
                    ++winmain_, c.regs[14], c.regs[13], c.cpsr);
            });

            Hook(tm, 0x11830u, 0xE1A0C00Du, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] try_params(\"%s\")\n",
                    Str(c, c.regs[0]).c_str());
            });

            Hook(tm, 0x11850u, 0xE58D0000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] fopen(params) -> 0x%08X\n",
                    c.regs[0]);
            });

            Hook(tm, 0x11438u, 0xE1A0C00Du, [this](const TraceContext& c) {
                LOG(Trace,
                    "[JLINEXEC] boot_linux(kernel=\"%s\", initrd=\"%s\")\n",
                    Str(c, c.regs[0]).c_str(), Str(c, c.regs[1]).c_str());
            });

            Hook(tm, 0x11454u, 0xE58D002Cu, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] fopen(kernel) -> 0x%08X\n",
                    c.regs[0]);
            });

            Hook(tm, 0x11C90u, 0xE59FC000u, [this](const TraceContext& c) {
                LOG(Trace,
                    "[JLINEXEC] VirtualCopy(dst=0x%08X src=0x%08X "
                    "size=0x%X flags=0x%X) lr=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3], c.regs[14]);
            });

            Hook(tm, 0x11CD8u, 0xE59FC000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] malloc(0x%X) lr=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });

            Hook(tm, 0x11CCCu, 0xE59FC000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] fread(buf=0x%08X size=0x%X cnt=%u)\n",
                    c.regs[0], c.regs[1], c.regs[2]);
            });

            Hook(tm, 0x11CF0u, 0xE59FC000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] SetKMode(%u)\n", c.regs[0]);
            });

            Hook(tm, 0x11D8Cu, 0xE59FC000u, [this](const TraceContext&) {
                LOG(Trace, "[JLINEXEC] MessageBoxW (WinMain fell through)\n");
            });

            Hook(tm, 0x11B88u, 0xE59F00E8u, [this](const TraceContext&) {
                LOG(Trace, "[JLINEXEC] POINT OF NO RETURN: cache flush + "
                           "jump to 0xC0008000\n");
            });

            Hook(tm, 0x11E2Cu, 0xE3A02000u, [this](const TraceContext&) {
                LOG(Trace, "[JLINEXEC] process exit path after WinMain\n");
            });

            Hook(tm, 0x11D98u, 0xE92D40F0u, [this](const TraceContext&) {
                LOG(Trace, "[JLINEXEC] start() process entry\n");
            });

            /* boot_linux dead-window sites (SetKMode .. first phys map) */
            Hook(tm, 0x11578u, 0xE3E00000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] SetKMode returned, r0=0x%X\n",
                    c.regs[0]);
            });

            Hook(tm, 0x11CE4u, 0xE59FC000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] SetProcPermissions(0x%X)\n",
                    c.regs[0]);
            });

            Hook(tm, 0x11580u, 0xE59F104Cu, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] SetProcPermissions returned, "
                           "r0=0x%X\n", c.regs[0]);
            });

            Hook(tm, 0x11D38u, 0xE59FC000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] fopen(\"%s\", \"%s\")\n",
                    Str(c, c.regs[0]).c_str(), Str(c, c.regs[1]).c_str());
            });

            Hook(tm, 0x1158Cu, 0xE58D0014u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] bootlog#3 fopen -> 0x%08X\n",
                    c.regs[0]);
            });

            Hook(tm, 0x11B54u, 0xE10F4000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] cpsr-switch entry, cpsr=0x%08X\n",
                    c.cpsr);
            });

            Hook(tm, 0x11B60u, 0xE1A0F00Eu, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] after MSR, cpsr=0x%08X lr=0x%08X\n",
                    c.cpsr, c.regs[14]);
            });

            Hook(tm, 0x115A8u, 0xE59D1030u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] cpsr-switch returned, cpsr=0x%08X\n",
                    c.cpsr);
            });

            Hook(tm, 0x113B4u, 0xE1A0C00Du, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] map_phys(pa=0x%08X size=0x%X)\n",
                    c.regs[0], c.regs[1]);
            });

            Hook(tm, 0x11C9Cu, 0xE59FC000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] VirtualAlloc(0x%X, 0x%X, 0x%X, "
                           "0x%X)\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3]);
            });

            Hook(tm, 0x115B8u, 0xE58D0024u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] map_phys(0xC0A00000) -> 0x%08X\n",
                    c.regs[0]);
            });

            /* MMU-off handoff chain: SCTLR=0x120 write, the jump to
               the physical trampoline, the trampoline itself, and the
               Linux kernel entry. */
            Hook(tm, 0x11B74u, 0xEE013F10u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] SCTLR write (MMU off), r3=0x%X "
                           "cpsr=0x%08X\n", c.regs[3], c.cpsr);
            });

            HookLoose(tm, 0x11B78u, 0xEE120F10u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] post-SCTLR insn +1 (MRC c2), "
                           "cpsr=0x%08X\n", c.cpsr);
            });

            HookLoose(tm, 0x11B80u, 0xE3A01103u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] post-SCTLR insn +3 (MOV R1), "
                           "cpsr=0x%08X\n", c.cpsr);
            });

            HookLoose(tm, 0x11B84u, 0xE1A0F001u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] MOV PC,R1 -> 0x%08X (post-MMU-off "
                           "jump)\n", c.regs[1]);
            });

            HookLoose(tm, 0xC0000000u, 0xE59F00E8u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] TRAMPOLINE at PA 0xC0000000 "
                           "executing, cpsr=0x%08X\n", c.cpsr);
            });

            HookLoose(tm, 0xC0008000u, 0xE1A00000u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] *** LINUX KERNEL ENTRY 0xC0008000 "
                           "*** cpsr=0x%08X r0=0x%X r1=0x%X r2=0x%X\n",
                    c.cpsr, c.regs[0], c.regs[1], c.regs[2]);
            });

            Hook(tm, 0x115ACu, 0xE3A00103u, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] at 0x115AC (post-switch insn 2), "
                           "sp=0x%08X\n", c.regs[13]);
            });

            Hook(tm, 0x115B4u, 0xEBFFFF7Eu, [this](const TraceContext& c) {
                LOG(Trace, "[JLINEXEC] at 0x115B4 BL map_phys, r0=0x%08X "
                           "r1=0x%08X\n", c.regs[0], c.regs[1]);
            });
        });
    }

private:
    /* OnPcFiltered admitting only jlinexec's code: the instruction word
       at the fired PC must match the IDA bytes. Caps detailed output at
       kMaxFires per hook so a 500-iteration guest loop stays readable. */
    void Hook(TraceManager& tm, uint32_t va, uint32_t sig,
              TraceHandler handler) {
        HookImpl(tm, va, sig, false, std::move(handler));
    }

    /* For hooks that fire after the guest turns the MMU off: ReadVa32
       peeks the data TLB only and returns nullopt under the identity
       regime, so a strict signature check silently drops every
       post-toggle fire. Unreadable PCs are admitted here. */
    void HookLoose(TraceManager& tm, uint32_t va, uint32_t sig,
                   TraceHandler handler) {
        HookImpl(tm, va, sig, true, std::move(handler));
    }

    void HookImpl(TraceManager& tm, uint32_t va, uint32_t sig,
                  bool admit_unreadable, TraceHandler handler) {
        auto fires = std::make_shared<uint32_t>(0);
        tm.OnPcFiltered(
            va,
            [sig, admit_unreadable](const TraceContext& c) {
                const auto w = c.ReadVa32(c.pc);
                if (!w) return admit_unreadable;
                return *w == sig;
            },
            [fires, handler = std::move(handler)](const TraceContext& c) {
                if (++*fires <= kMaxFires)
                    handler(c);
                else if (*fires % 100u == 0u)
                    LOG(Trace, "[JLINEXEC] (pc=0x%08X fired %u times)\n",
                        c.pc, *fires);
            });
    }

    static std::string Str(const TraceContext& c, uint32_t va) {
        std::string out;
        for (uint32_t i = 0; i < 96; ++i) {
            const auto b = c.ReadVa8(va + i);
            if (!b || !*b) break;
            if (*b >= 0x20 && *b < 0x7F) {
                out += static_cast<char>(*b);
            } else {
                char esc[8];
                std::snprintf(esc, sizeof esc, "\\x%02X", *b);
                out += esc;
            }
        }
        return out;
    }

    static constexpr uint32_t kMaxFires = 60;
    uint32_t winmain_ = 0;
};
REGISTER_SERVICE(JlinexecBootProbe);

}  /* namespace */

#endif  /* CERF_DEV_MODE */
