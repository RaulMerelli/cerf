#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <string>

#if CERF_DEV_MODE

namespace {

/* SL4 nk.exe nulls OEMWriteDebugString (sink sub_800B20D8 emits each char via
   the empty nullsub_3), so no serial debug text exists. 0x800835BC is the
   wide-string sink entry (R0 = formatted string) every debug print passes
   through before the dead emit — the only point to recover kernel debug text. */
class SimpadSl4OemDebugPrint : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            tm.OnPc(0x800835BCu, [](const TraceContext& c) {
                const uint32_t base = c.regs[0];
                std::string s;
                for (uint32_t i = 0; i < 512u; ++i) {
                    auto w = c.ReadVa16(base + 2u * i);
                    if (!w || *w == 0) break;
                    if (*w >= 0x20 && *w < 0x7F) s.push_back(char(*w));
                }
                LOG(Trace, "[NKDBG] %s\n", s.c_str());
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4OemDebugPrint);

#endif  // CERF_DEV_MODE
