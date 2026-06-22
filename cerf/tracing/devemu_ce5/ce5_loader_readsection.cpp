#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../jit/arm/arm_mmu_state.h"
#include "devemu_ce5_bundle.h"

namespace {

/* RE'd nk.exe VAs (base 0x80001000, IDA VA == runtime kernel VA) for the
   slot-0 .data load failure. ReadSection sub_80029C44 returns 14
   (ERROR_OUTOFMEMORY) when its VirtualAlloc(realaddr, MEM_COMMIT) fails.
   LoadO32 BL it at 0x8002A240; at 0x8002A244 R0=result, R7=o32_lite*
   (+0 vsize, +4 rva, +8 realaddr, +16 flags). ReadSection entry: R0=oeptr
   (+6 pagemode halfword), R1=o32_lite. cerf_guest: code/RO realaddr
   0x020Cxxxx, .data 0x01DF8000. */

constexpr uint32_t kReadSectionEntry   = 0x80029C44u;
constexpr uint32_t kLoadO32ResultSite  = 0x8002A244u;
constexpr uint32_t kGuestLoBase        = 0x01DF0000u;
constexpr uint32_t kGuestHiBase        = 0x02100000u;

bool InGuestSpan(uint32_t realaddr) {
    return realaddr >= kGuestLoBase && realaddr < kGuestHiBase;
}

class TraceCe5LoaderReadSection : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kDevemuCe5BundleCrc32, [&tm] {
            tm.OnPc(kReadSectionEntry, [](const TraceContext& c) {
                const uint32_t oeptr = c.regs[0];
                const uint32_t o32   = c.regs[1];
                auto realaddr = c.ReadVa32(o32 + 8);
                if (!realaddr || !InGuestSpan(*realaddr)) return;
                auto rva      = c.ReadVa32(o32 + 4);
                auto vsize    = c.ReadVa32(o32 + 0);
                auto flags    = c.ReadVa32(o32 + 16);
                auto pagemode = c.ReadVa16(oeptr + 6);
                auto filetype = c.ReadVa32(oeptr + 4);
                LOG(Trace, "[CE5_RS] ReadSection ENTER realaddr=0x%08X rva=0x%05X "
                           "vsize=0x%05X flags=0x%08X pagemode=%u filetype=0x%08X "
                           "pid=0x%08X\n",
                    *realaddr, rva.value_or(0), vsize.value_or(0),
                    flags.value_or(0), (unsigned)pagemode.value_or(0xFFFFu),
                    filetype.value_or(0),
                    c.emu.Get<ArmMmu>().State()->process_id);
            });

            tm.OnPc(kLoadO32ResultSite, [](const TraceContext& c) {
                const uint32_t r0  = c.regs[0];
                const uint32_t o32 = c.regs[7];
                auto realaddr = c.ReadVa32(o32 + 8);
                const bool guest = realaddr.has_value() && InGuestSpan(*realaddr);
                if (r0 == 0 && !guest) return;
                auto flags = c.ReadVa32(o32 + 16);
                LOG(Trace, "[CE5_RS] LoadO32 ReadSection RESULT=%u (0=ok "
                           "14=OUTOFMEM 193=BADEXE) realaddr=0x%08X flags=0x%08X%s\n",
                    r0, realaddr.value_or(0), flags.value_or(0),
                    (r0 != 0) ? "  <<< FAILED" : "");
            });
        });
    }
};

REGISTER_SERVICE(TraceCe5LoaderReadSection);

}  /* namespace */
