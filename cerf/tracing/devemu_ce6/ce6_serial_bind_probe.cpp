#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "devemu_ce6_bundle.h"

#if CERF_DEV_MODE

#include <cstdint>
#include <string>

namespace {

/* pcmcia.dll serial-card bind tail (exec VA = extracted-PE link VA):
   FindPCCardDriver sub_C08E6F50 -> CreatePnpEntry sub_C08E6D38 -> LoadDriver
   sub_C08E64B8. */
class Ce6SerialBindProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kDevemuCe6BundleCrc32, [this] {
            auto& t = emu_.Get<TraceManager>();

            auto wstr = [](const TraceContext& c, uint32_t va) {
                std::string s;
                for (int i = 0; i < 96; ++i) {
                    const auto w = c.ReadVa16(va + 2u * i);
                    if (!w || !*w) break;
                    s += (*w >= 0x20u && *w < 0x7Fu)
                        ? static_cast<char>(*w) : '?';
                }
                return s;
            };

            /* CreatePnpEntry(a1=r0 CPcmciaLoad*, a2=r1 lpDetectKey). The PnP-id
               key being created is m_InstancePath at a1+556. */
            t.OnPc(0xC08E6D38u, [wstr](const TraceContext& c) {
                LOG(Trace, "[CE6SER] CreatePnpEntry template='%s' instance='%s'\n",
                    wstr(c, c.regs[1]).c_str(), wstr(c, c.regs[0] + 556u).c_str());
            });
            /* Return site of the CreatePnpEntry call in FindPCCardDriver:
               r0 = result (0 => template open or copy failed). */
            t.OnPc(0xC08E703Cu, [](const TraceContext& c) {
                LOG(Trace, "[CE6SER] CreatePnpEntry -> %d\n", (int)c.regs[0]);
            });
            /* LoadDriver: reached only when the recursion proceeds (i.e.
               CreatePnpEntry succeeded and FindRegKeyFromPnp now matches). */
            t.OnPc(0xC08E64B8u, [](const TraceContext&) {
                LOG(Trace, "[CE6SER] *** LoadDriver (binding the COM driver) ***\n");
            });
            /* Return site of the ActivateDeviceEx call inside LoadDriver:
               r0 = device handle (0 => activation failed, no COMx created). */
            t.OnPc(0xC08E678Cu, [](const TraceContext& c) {
                LOG(Trace, "[CE6SER] ActivateDeviceEx -> 0x%X\n", c.regs[0]);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Ce6SerialBindProbe);

#endif  /* CERF_DEV_MODE */
