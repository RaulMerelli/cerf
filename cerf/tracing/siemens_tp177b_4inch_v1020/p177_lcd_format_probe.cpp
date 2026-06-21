#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* P177 8bpp encoding probe (dev, one-shot): ddi.dll sub_3464338's R0/R1/R2 are
   the GPE surface color masks - settling 3:3:2-direct (E0/1C/03) vs palettized
   (0/0/0). Unfiltered OnPc is exact - this display-init setup runs only in the
   display host (gwes) at boot; fired_ takes the first hit. ddi.dll XIP → link VA. */

namespace {

class TraceSiemensP177LcdFormat : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSiemensTp177bBundleCrc32, [this, &tm] {
            tm.OnPc(0x3464338u, [this](const TraceContext& c) {
                if (fired_) return;
                fired_ = true;
                LOG(Trace, "[P177LCDFMT] color masks R=0x%08X G=0x%08X B=0x%08X "
                           "(LR=0x%08X)\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[14]);
            });
        });
    }

private:
    bool fired_ = false;
};

REGISTER_SERVICE(TraceSiemensP177LcdFormat);

}  /* namespace */
