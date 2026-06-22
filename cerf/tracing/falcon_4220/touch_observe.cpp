#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "bundle.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

/* Touch diagnosis, pinned to device.exe (slot 3, where touch.dll runs):
   sub_18E2288 = IST wake; TouchPanelCalibrateAPoint = decode + a one-shot dump
   of the driver's live cal flag/coeffs to check against CERF's RawFromScreen. */
class Falcon4220TouchObserve : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            static std::atomic<uint64_t> wakes{0};
            static std::atomic<uint64_t> decs{0};
            /* Admit only where touch.dll's cal page is mapped in the current
               address space, so the coeff ReadVa32s are valid - the cal runs in
               touch.dll's host process, which is NOT a fixed slot. */
            const TracePredicate touch_mapped = [](const TraceContext& c) {
                return c.ReadVa32(0x18E730Cu).has_value();
            };
            tm.OnPcFiltered(0x18E2288u, touch_mapped, [](const TraceContext&) {
                const uint64_t k = wakes.fetch_add(1, std::memory_order_relaxed);
                if ((k & 3u) == 0u)
                    LOG(Trace, "[FALCON] IST wake #%llu\n",
                        static_cast<unsigned long long>(k));
            });
            tm.OnPcFiltered(0x18E2C64u, touch_mapped, [](const TraceContext& c) {
                const uint64_t k = decs.fetch_add(1, std::memory_order_relaxed);
                static std::atomic<bool> dumped{false};
                if (!dumped.exchange(true, std::memory_order_relaxed))
                    LOG(Trace, "[FALCON] CAL slot=%u flag=%d M=[%d,%d,%d / %d,%d,%d] DIV=%d\n",
                        c.emu.Get<ArmMmu>().State()->process_id >> 25,
                        static_cast<int>(c.ReadVa32(0x18E730Cu).value_or(0xFFFFFFFFu)),
                        static_cast<int>(c.ReadVa32(0x18E72F0u).value_or(0)),
                        static_cast<int>(c.ReadVa32(0x18E72F4u).value_or(0)),
                        static_cast<int>(c.ReadVa32(0x18E72F8u).value_or(0)),
                        static_cast<int>(c.ReadVa32(0x18E72FCu).value_or(0)),
                        static_cast<int>(c.ReadVa32(0x18E7300u).value_or(0)),
                        static_cast<int>(c.ReadVa32(0x18E7304u).value_or(0)),
                        static_cast<int>(c.ReadVa32(0x18E7308u).value_or(0)));
                if ((k & 3u) == 0u)
                    LOG(Trace, "[FALCON] decode #%llu raw=(%d,%d)\n",
                        static_cast<unsigned long long>(k),
                        static_cast<int>(c.regs[0]), static_cast<int>(c.regs[1]));
            });
            /* gwes.exe sub_398D4 (GWES = slot 5) = TouchPanelEnable's injector:
               r0=flags(bit1 down), r1=x, r2=y = the screen point GWES injects -
               the actual landing. Shows phantom injects + each click's coord. */
            const TracePredicate in_gwes = [](const TraceContext& c) {
                return (c.emu.Get<ArmMmu>().State()->process_id >> 25) == 5u;
            };
            /* GWES dispatch chain: sub_20FA0 dispatches a dequeued event,
               sub_2147C posts the window message. 20FA0 firing without a
               following 2147C => the GWES input thread wedged mid-dispatch. */
            tm.OnPcFiltered(0x20FA0u, in_gwes, [](const TraceContext& c) {
                LOG(Trace, "[FALCON] gwes-dispatch act=%d x=%d y=%d\n",
                    static_cast<int>(c.regs[0]), static_cast<int>(c.regs[1]),
                    static_cast<int>(c.regs[2]));
            });
            tm.OnPcFiltered(0x2147Cu, in_gwes, [](const TraceContext& c) {
                LOG(Trace, "[FALCON] gwes-msgpost code=%d x=%d y=%d\n",
                    static_cast<int>(c.regs[0]), static_cast<int>(c.regs[1]),
                    static_cast<int>(c.regs[2]));
            });
            tm.OnPcFiltered(0x398D4u, in_gwes, [](const TraceContext& c) {
                /* free = dword_B9FE8 (ring free slots), wr = dword_BC020 (write
                   idx), rd = dword_B9FC4 (read idx). free<2 => DOWN dropped; a
                   frozen rd while wr climbs => consumer sub_39C28 stalled. */
                LOG(Trace, "[FALCON] gwes-inject flags=0x%X x=%d y=%d free=%d wr=%d rd=%d\n",
                    static_cast<unsigned>(c.regs[0] & 0xFFu),
                    static_cast<int>(c.regs[1]), static_cast<int>(c.regs[2]),
                    static_cast<int>(c.ReadVa32(0xB9FE8u).value_or(0xFFFFFFFFu)),
                    static_cast<int>(c.ReadVa32(0xBC020u).value_or(0xFFFFFFFFu)),
                    static_cast<int>(c.ReadVa32(0xB9FC4u).value_or(0xFFFFFFFFu)));
            });
            /* touch.dll sample-ring overflow flag (sub_18E37F0 sets dword_18E734C=1
               when the 500-entry ring is full and drops the sample). */
            tm.OnRunLoopIter([](const TraceContext& c) {
                static std::atomic<uint32_t> last_ovf{0xFFFFFFFFu};
                auto ovf = c.ReadVa32(0x18E734Cu);
                if (!ovf) return;   /* device.exe not the current address space. */
                const uint32_t prev = last_ovf.exchange(*ovf, std::memory_order_relaxed);
                if (*ovf != prev)
                    LOG(Trace, "[FALCON] sample-ring overflow %u->%u head=%u\n",
                        prev, *ovf, c.ReadVa32(0x18E901Cu).value_or(0xFFFFFFFFu));
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(Falcon4220TouchObserve);

#endif  // CERF_DEV_MODE
