#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* Watches touch.dll's CalibrateAPoint: logs the raw ADC it receives, the live
   affine matrix (dword_FB4130..414C), and the screen point it computes. D=0
   means align-screen is still collecting raw points; once the user finishes
   calibration D goes non-zero and screen=(4*(A.raw+C)/D) should track the tap. */
class TouchProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            emu_.Get<TraceManager>().OnPc(0xFB2784u, [this](const TraceContext& c) {
                auto rd = [&](uint32_t va) { return (int32_t)c.ReadVa32(va).value_or(0); };
                const int rx = (int)c.regs[0], ry = (int)c.regs[1];
                const int d = rd(0xFB4148u);
                /* Throttle: log on D change and at most every 16th point. */
                if (d == last_d_ && (++count_ & 0xF) != 0) return;
                last_d_ = d;
                int sx = rx, sy = ry;
                if (d) {
                    sx = 4 * (rd(0xFB4130u) * rx + rd(0xFB4134u) * ry + rd(0xFB4138u)) / d;
                    sy = 4 * (rd(0xFB413Cu) * rx + rd(0xFB4140u) * ry + rd(0xFB4144u)) / d;
                }
                LOG(Trace, "[TOUCHPROBE] raw=(%d,%d) D=%d -> screen=(%d,%d)\n",
                    rx, ry, d, sx, sy);
            });
        });
    }

private:
    int      last_d_ = -1;
    uint32_t count_  = 0;
};

}  /* namespace */

REGISTER_SERVICE(TouchProbe);

#endif  /* CERF_DEV_MODE */
