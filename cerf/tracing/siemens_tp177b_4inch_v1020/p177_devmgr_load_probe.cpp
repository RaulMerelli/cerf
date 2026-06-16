#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* P177 device.exe-park probe (dev): the BuiltIn driver loaded after miscio
   blocks in its Init, so drivers-ready never signals. busenum.dll's per-driver
   loader sub_3F32300 (run only by device.exe -> unfiltered OnPc is exact) holds
   the driver-key wstring at *(r0+80); the LAST [DEVLOAD] key names the culprit. */

namespace {

class TraceSiemensP177DevmgrLoad : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSiemensTp177bBundleCrc32, [this, &tm] {
            tm.OnPc(0x3F32300u, [this](const TraceContext& c) {
                if (fired_ >= 80) return;
                ++fired_;
                char key[48] = {0};
                if (auto namep = c.ReadVa32(c.regs[0] + 80u)) {
                    for (int i = 0; i < 47; ++i) {
                        auto wc = c.ReadVa16(*namep + (uint32_t)(i * 2));
                        if (!wc || !*wc) break;
                        key[i] = (*wc < 128u) ? (char)*wc : '?';
                    }
                }
                LOG(Trace, "[DEVLOAD] seq=%d key='%s'\n", fired_, key);
            });
        });
    }

private:
    int fired_ = 0;
};

REGISTER_SERVICE(TraceSiemensP177DevmgrLoad);

}  /* namespace */
