#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"
#include "../../jit/arm_mmu_state.h"
#include "odo_bundle.h"

#if CERF_DEV_MODE

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace {

/* GA boot data-aborts in gwes.exe: FreeOneLibraryPart2 (WINCE300
   LOADER.C:1389) is entered with a garbage pMod (R4=BVA=0x078CDEE0).
   The chain hooks name which library load fails and who passes the
   bad pMod. */
constexpr uint32_t kLoadOneLibrary      = 0x8C618764u;  /* (name|special, paging, wFlags) */
constexpr uint32_t kDoImports           = 0x8C616680u;
constexpr uint32_t kCallDLLEntry        = 0x8C616298u;  /* (pMod, reason, ...) */
constexpr uint32_t kFreeOneLibraryPart2 = 0x8C6163ACu;  /* LOADER.C:1389 */
constexpr uint32_t kFreeOneLibrary      = 0x8C6165A8u;  /* breadcrumb wrapper */
constexpr uint32_t kFreeLibraryByName   = 0x8C6165E0u;  /* (LPCHAR ascii) */

constexpr uint32_t kModNameOff = 8u;  /* Module::lpszModName */

class TraceOdoGaFreelibCorruption : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kOdoBundleCrc32, [this] {
            auto& tm = emu_.Get<TraceManager>();

            Hook(tm, kLoadOneLibrary, [](const TraceContext& c) {
                LOG(Trace, "[GAFREE] LoadOneLibrary(\"%s\") paging=%u "
                           "wFlags=0x%X lr=0x%08X pid=0x%08X\n",
                    WStr(c, c.regs[0] & ~1u).c_str(), c.regs[1], c.regs[2],
                    c.regs[14], Pid(c));
            });

            Hook(tm, kDoImports, [](const TraceContext& c) {
                LOG(Trace, "[GAFREE] DoImports pMod=0x%08X (\"%s\") a2=0x%08X "
                           "base=0x%08X lr=0x%08X\n",
                    c.regs[0], ModName(c, c.regs[0]).c_str(), c.regs[1],
                    c.regs[2], c.regs[14]);
            });

            Hook(tm, kCallDLLEntry, [](const TraceContext& c) {
                LOG(Trace, "[GAFREE] CallDLLEntry pMod=0x%08X (\"%s\") "
                           "reason=%u lr=0x%08X pid=0x%08X\n",
                    c.regs[0], ModName(c, c.regs[0]).c_str(), c.regs[1],
                    c.regs[14], Pid(c));
            });

            Hook(tm, kFreeOneLibraryPart2, [](const TraceContext& c) {
                LOG(Trace, "[GAFREE] FreeOneLibraryPart2 pMod=0x%08X (\"%s\") "
                           "callEntry=%u lr=0x%08X pid=0x%08X\n",
                    c.regs[0], ModName(c, c.regs[0]).c_str(), c.regs[1],
                    c.regs[14], Pid(c));
            });

            Hook(tm, kFreeOneLibrary, [](const TraceContext& c) {
                LOG(Trace, "[GAFREE] FreeOneLibrary pMod=0x%08X (\"%s\") "
                           "callEntry=%u lr=0x%08X\n",
                    c.regs[0], ModName(c, c.regs[0]).c_str(), c.regs[1],
                    c.regs[14]);
            });

            Hook(tm, kFreeLibraryByName, [](const TraceContext& c) {
                LOG(Trace, "[GAFREE] FreeLibraryByName(\"%s\") lr=0x%08X\n",
                    AStr(c, c.regs[0]).c_str(), c.regs[14]);
            });
        });
    }

private:
    static uint32_t Pid(const TraceContext& c) {
        return c.emu.Get<ArmMmu>().State()->process_id;
    }

    static std::string ModName(const TraceContext& c, uint32_t pmod) {
        const auto pname = c.ReadVa32(pmod + kModNameOff);
        if (!pname) return "<pMod unreadable>";
        return WStr(c, *pname);
    }

    /* Per-hook fire cap so a free/load storm can't flood the log. */
    static void Hook(TraceManager& tm, uint32_t va, TraceHandler handler) {
        auto fires = std::make_shared<uint32_t>(0);
        tm.OnPcFiltered(
            va,
            [](const TraceContext&) { return true; },
            [fires, handler = std::move(handler)](const TraceContext& c) {
                if (++*fires <= kMaxFires) handler(c);
            });
    }

    static std::string WStr(const TraceContext& c, uint32_t va) {
        std::string out;
        for (uint32_t i = 0; i < 96; ++i) {
            const auto w = c.ReadVa16(va + i * 2);
            if (!w) { out += i ? "" : "<unreadable>"; break; }
            if (!*w) break;
            if (*w >= 0x20 && *w < 0x7F) {
                out += static_cast<char>(*w);
            } else {
                char esc[8];
                std::snprintf(esc, sizeof esc, "\\u%04X", *w);
                out += esc;
            }
        }
        return out;
    }

    static std::string AStr(const TraceContext& c, uint32_t va) {
        std::string out;
        for (uint32_t i = 0; i < 96; ++i) {
            const auto b = c.ReadVa8(va + i);
            if (!b) { out += i ? "" : "<unreadable>"; break; }
            if (!*b) break;
            out += (*b >= 0x20 && *b < 0x7F) ? static_cast<char>(*b) : '?';
        }
        return out;
    }

    static constexpr uint32_t kMaxFires = 300;
};
REGISTER_SERVICE(TraceOdoGaFreelibCorruption);

}  /* namespace */

#endif  /* CERF_DEV_MODE */
