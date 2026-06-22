#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "bundle.h"

#include <cstring>

#if CERF_DEV_MODE

namespace {

void ReadAscii(const TraceContext& c, uint32_t va, char* out, int cap) {
    int n = 0;
    for (; n < cap - 1; ++n) {
        auto b = c.ReadVa8(va + n);
        if (!b || *b == 0) break;
        out[n] = static_cast<char>(*b);
    }
    out[n] = 0;
}

void ReadWide(const TraceContext& c, uint32_t va, char* out, int cap) {
    int n = 0;
    for (; n < cap - 1; ++n) {
        auto w = c.ReadVa16(va + n * 2u);
        if (!w || *w == 0) break;
        out[n] = (*w < 0x80u) ? static_cast<char>(*w) : '?';
    }
    out[n] = 0;
}

/* GetProcAddressA/W (verified coredll VAs). When devmgr resolves trueffs's
   DSK_Init, R0 = hModule = trueffs_G3's runtime base; needed to hook DSK_Init's
   internal flow (IDA base 0x10000000) at correct runtime VAs. */
class FalconDocWindowProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            TracePredicate device_exe = [](const TraceContext& c) {
                return (c.emu.Get<ArmMmu>().State()->process_id >> 25) == 3u;
            };
            tm.OnPcFiltered(0x3F842A4u, device_exe, [](const TraceContext& c) {
                char n[32]; ReadAscii(c, c.regs[1], n, 32);
                if (std::strstr(n, "DSK"))
                    LOG(Trace, "[FALCON-DOC] GetProcAddressA '%s' hModule=0x%08X "
                        "LR=0x%08X\n", n, c.regs[0], c.regs[14]);
            });
            tm.OnPcFiltered(0x3F84394u, device_exe, [](const TraceContext& c) {
                char n[32]; ReadWide(c, c.regs[1], n, 32);
                if (std::strstr(n, "DSK"))
                    LOG(Trace, "[FALCON-DOC] GetProcAddressW '%s' hModule=0x%08X "
                        "LR=0x%08X\n", n, c.regs[0], c.regs[14]);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconDocWindowProbe);

#endif  /* CERF_DEV_MODE */
