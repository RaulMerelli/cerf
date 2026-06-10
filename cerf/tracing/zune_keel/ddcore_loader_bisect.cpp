#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <atomic>
#include <cstdint>
#include <memory>

#if CERF_DEV_MODE

namespace {

/* DirectDrawCreate loader sub_376511C: HALInit -> validator sub_3764D78 -> post-
   validator gates; on Zune it returns 0 and gemstone retries forever. sub_37634A8
   fires only if the validator passed, so last gate to fire + first silent = bail. */
class ZuneDdcoreLoaderBisect : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kZuneKeelBundleCrc32, [&] {
            struct Gate { uint32_t ea; const char* name; };
            static const Gate kGates[] = {
                {0x376511Cu, "loader sub_376511C ENTRY"},
                {0x37634A8u, "post-validator (=> validator passed)"},
                {0x3765738u, "else-branch (mode setup) entered"},
                {0x376561Cu, "loc_376561C merge (316-copy)"},
                {0x37656F0u, "PRE mode-deref LDR[lpModeInfo]"},
                {0x376571Cu, "POST mode-deref (bpp check)"},
                {0x37659C0u, "loc_37659C0 (near end)"},
                {0x3765AE0u, "PRE BL sub_3763CC0"},
                {0x3765AE8u, "gate sub_3763CC0 reached"},
                {0x3765C54u, "LABEL_162 FAIL (loader returns 0)"},
            };
            for (const auto& g : kGates) {
                const char* nm = g.ea ? g.name : nullptr;
                auto cnt = std::make_shared<std::atomic<uint32_t>>(0);
                tm.OnPc(g.ea, [nm, cnt](const TraceContext& c) {
                    if (cnt->fetch_add(1) >= 6) return;
                    LOG(Trace, "[DDLOADER] %s  lr=0x%08X\n", nm, c.regs[14]);
                });
            }

            /* sub_37634A8 return-0 path (dwFlags bit 0x4 clear): which MemAlloc fails. */
            struct AllocPc { uint32_t ea; const char* name; };
            static const AllocPc kAllocs[] = {
                {0x3763560u, "MemAlloc#1(28) R0 result -> a1[372]"},
                {0x3763578u, "MemAlloc#2(24) R0 result -> a1[409]"},
                {0x3763588u, "both allocs OK -> dwFlags load (gate would return 1)"},
            };
            for (const auto& a : kAllocs) {
                const char* nm = a.name;
                auto cnt = std::make_shared<std::atomic<uint32_t>>(0);
                tm.OnPc(a.ea, [nm, cnt](const TraceContext& c) {
                    if (cnt->fetch_add(1) >= 6) return;
                    LOG(Trace, "[DDALLOC] %s  R0=0x%08X lr=0x%08X\n",
                        nm, c.regs[0], c.regs[14]);
                });
            }

            /* LABEL_59 alignment-caps bail (R6=a1). Unfiltered like the gates above:
               only the DirectDrawCreate caller runs this loader, and a1[57]/aligns
               read our process-independent HAL fill. */
            {
                auto cnt = std::make_shared<std::atomic<uint32_t>>(0);
                tm.OnPc(0x3765524u, [cnt](const TraceContext& c) {
                    uint32_t n = cnt->fetch_add(1);
                    if (n >= 4) return;
                    uint32_t a1 = c.regs[6];
                    LOG(Trace, "[DDALIGN] a1[57]=0x%08X off=0x%08X ovl=0x%08X "
                               "tex=0x%08X zbuf=0x%08X\n",
                        c.regs[2],
                        c.ReadVa32(a1 + 0x44).value_or(0xDEADBEEFu),
                        c.ReadVa32(a1 + 0x48).value_or(0xDEADBEEFu),
                        c.ReadVa32(a1 + 0x4C).value_or(0xDEADBEEFu),
                        c.ReadVa32(a1 + 0x50).value_or(0xDEADBEEFu));
                    if (n == 0) {
                        for (uint32_t i = 0; i < 115; i += 5)
                            LOG(Trace, "[DDDUMP] a1[%3u..]=%08X %08X %08X %08X %08X\n",
                                i,
                                c.ReadVa32(a1 + 4 * i).value_or(0xDEADBEEFu),
                                c.ReadVa32(a1 + 4 * (i + 1)).value_or(0xDEADBEEFu),
                                c.ReadVa32(a1 + 4 * (i + 2)).value_or(0xDEADBEEFu),
                                c.ReadVa32(a1 + 4 * (i + 3)).value_or(0xDEADBEEFu),
                                c.ReadVa32(a1 + 4 * (i + 4)).value_or(0xDEADBEEFu));
                    }
                });
            }
            {
                auto cnt = std::make_shared<std::atomic<uint32_t>>(0);
                tm.OnPc(0x3765594u, [cnt](const TraceContext& c) {
                    if (cnt->fetch_add(1) >= 4) return;
                    LOG(Trace, "[DDALIGN] PAST alignment-caps cluster (caps passed) "
                               "lr=0x%08X\n", c.regs[14]);
                });
            }
        });
    }
};

}  // namespace

REGISTER_SERVICE(ZuneDdcoreLoaderBisect);

#endif  // CERF_DEV_MODE
