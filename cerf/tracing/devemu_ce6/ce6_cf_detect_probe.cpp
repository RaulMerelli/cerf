#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "devemu_ce6_bundle.h"

#if CERF_DEV_MODE

#include <cstdint>
#include <string>

namespace {

/* pcmcia.dll on this image is pcc_pcm.dll (DEVICEEMULATOR platform.bib):
   binding walks HKLM\Drivers\PCMCIA\Detect\NN in RunDetectors
   (sub_C08E686C; exec VA = extracted-PE link VA). Pinned: the hive has
   only the SERIAL + NE2000 detectors — no disk detector, CF can't bind. */
class Ce6CfDetectProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kDevemuCe6BundleCrc32, [this] {
            auto& t = emu_.Get<TraceManager>();

            auto wstr = [](const TraceContext& c, uint32_t va) {
                std::string s;
                for (int i = 0; i < 48; ++i) {
                    const auto w = c.ReadVa16(va + 2u * i);
                    if (!w || !*w) break;
                    s += (*w >= 0x20u && *w < 0x7Fu)
                        ? static_cast<char>(*w) : '?';
                }
                return s;
            };
            t.OnPc(0xC08E686Cu, [](const TraceContext& c) {
                LOG(Trace, "[CE6DET] RunDetectors enter r0=0x%X r1=0x%X\n",
                    c.regs[0], c.regs[1]);
            });
            t.OnPc(0xC08E696Cu, [](const TraceContext& c) {
                LOG(Trace, "[CE6DET] RegEnumKeyEx result=0x%X\n", c.regs[0]);
            });
            t.OnPc(0xC08E69D8u, [wstr](const TraceContext& c) {
                LOG(Trace, "[CE6DET] LoadDriver('%s')\n",
                    wstr(c, c.regs[0]).c_str());
            });
            t.OnPc(0xC08E69DCu, [](const TraceContext& c) {
                LOG(Trace, "[CE6DET] LoadDriver -> 0x%X\n", c.regs[0]);
            });
            t.OnPc(0xC08E69F0u, [](const TraceContext& c) {
                LOG(Trace, "[CE6DET] GetProcAddress -> 0x%X\n", c.regs[0]);
            });
            t.OnPc(0xC08E6A4Cu, [](const TraceContext& c) {
                LOG(Trace, "[CE6DET] detect entry -> 0x%X\n", c.regs[0]);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Ce6CfDetectProbe);

#endif  /* CERF_DEV_MODE */
