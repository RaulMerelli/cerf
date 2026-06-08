#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <atomic>
#include <cstdint>

#if CERF_DEV_MODE

namespace {

class IpaqH3100OptPackProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kH3100Ppc2000BundleCrc32, [&] {
            /* Every MicroP message the OAL dispatches (msg_id at 0xAC020008). */
            tm.OnPc(0x80059C54u, [](const TraceContext& c) {
                static std::atomic<uint64_t> n{0};
                const uint64_t k = n.fetch_add(1, std::memory_order_relaxed);
                if (k >= 60 && (k & 0x3Fu) != 0u) return;
                auto id = c.ReadVa32(0xAC020008u);
                LOG(Trace, "[H3100_OAL_RX] k=%llu msg_id=%d\n",
                    static_cast<unsigned long long>(k),
                    id ? static_cast<int>(*id) : -1);
            });

            /* OAL TX framer sub_80059AC8. Reads marker [*0x8C06B6C0+36],
               msg_id +40, len +44, SSC trigger [*0x8C06B6C4+12], TXRDY +32.
               marker==0x3EE + msg_id==11 == the ppcutil readIIC transmit. */
            tm.OnPc(0x80059AC8u, [](const TraceContext& c) {
                static std::atomic<uint64_t> n{0};
                const uint64_t k = n.fetch_add(1, std::memory_order_relaxed);
                if (k >= 250 && (k & 0x3Fu) != 0u) return;
                auto sb = c.ReadVa32(0x8C06B6C0u);
                auto cb = c.ReadVa32(0x8C06B6C4u);
                int marker = -1, mid = -1, len = -1, trig = -1, sts = -1;
                if (sb) {
                    if (auto m = c.ReadVa32(*sb + 36u)) marker = (int)*m;
                    if (auto v = c.ReadVa8 (*sb + 40u)) mid    = *v;
                    if (auto v = c.ReadVa8 (*sb + 44u)) len    = *v;
                }
                if (cb) {
                    if (auto v = c.ReadVa32(*cb + 12u)) trig = (int)*v;
                    if (auto v = c.ReadVa32(*cb + 32u)) sts  = (int)*v;
                }
                LOG(Trace, "[H3100_FRAMER] k=%llu marker=0x%X msg_id=%d len=%d "
                           "trig=0x%X sts=0x%X\n",
                    static_cast<unsigned long long>(k),
                    marker, mid, len, trig, sts);
            });

            /* keybddr IST entry: does it run, and what are the action flags
               [769]=keypress(0xAC020301) [792]=packplug(0xAC020318)
               [793]=packunplug(0xAC020319). */
            tm.OnPc(0xF31754u, [](const TraceContext& c) {
                static std::atomic<uint64_t> n{0};
                const uint64_t k = n.fetch_add(1, std::memory_order_relaxed);
                if (k >= 20 && (k & 0xFFu) != 0u) return;
                auto f769 = c.ReadVa8(0xAC020301u);
                auto f792 = c.ReadVa8(0xAC020318u);
                auto f793 = c.ReadVa8(0xAC020319u);
                LOG(Trace, "[H3100_KEYBDDR_IST] k=%llu key=%d packplug=%d "
                           "packunplug=%d\n",
                    static_cast<unsigned long long>(k),
                    f769 ? *f769 : -1, f792 ? *f792 : -1, f793 ? *f793 : -1);
            });

            /* The EventOptionPackPlug signal path (converged, just before the
               EventModify at 0xF31AE0). Firing == OS reports a pack change. */
            tm.OnPc(0xF31AD4u, [](const TraceContext& c) {
                auto type = c.ReadVa8(0xAC02030Cu);   /* sleeve-type */
                auto hs   = c.ReadVa8(0xAC02035Au);   /* H3100 handshake byte */
                LOG(Trace, "[H3100_OPTPACK_SIGNAL] EventOptionPackPlug signalled "
                           "sleeve_type=%d handshake=%d\n",
                    type ? *type : -1, hs ? *hs : -1);
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(IpaqH3100OptPackProbe);

#endif
