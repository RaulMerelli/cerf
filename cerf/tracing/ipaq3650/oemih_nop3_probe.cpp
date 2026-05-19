#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../socs/sa1110/sa1110_intc.h"
#include "../../socs/sa1110/sa1110_lcd.h"
#include "bundle.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

constexpr int kMaxFiresPerSite = 30;

class IpaqOemihNop3Probe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kIpaq3650BundleCrc32, [&] {
            static std::atomic<int> entry_fires{0};
            static std::atomic<int> nop3_fires{0};
            static std::atomic<int> nop1_fires{0};

            tm.OnPc(0x800AA1BCu, [](const TraceContext& c) {
                const int n = entry_fires.fetch_add(1) + 1;
                if (n > kMaxFiresPerSite) return;
                auto& intc = c.emu.Get<Sa1110Intc>();
                LOG(Trace, "[OEMIH] ENTRY #%d  ICIP=0x%08X ICMR=0x%08X "
                           "ICPR=0x%08X ICLR=0x%08X  R0=0x%08X LR=0x%08X "
                           "CPSR=0x%08X\n",
                    n, intc.GetIcIp(), intc.GetIcmr(), intc.GetIcpr(),
                    intc.GetIclr(), c.regs[0], c.regs[14], c.cpsr);
            });

            tm.OnPc(0x800AABA8u, [](const TraceContext& c) {
                const int n = nop3_fires.fetch_add(1) + 1;
                if (n > kMaxFiresPerSite) return;
                auto& intc = c.emu.Get<Sa1110Intc>();
                LOG(Trace, "[OEMIH] NOP3-FALLTHROUGH #%d  ICIP=0x%08X "
                           "ICMR=0x%08X ICPR=0x%08X LR=0x%08X\n",
                    n, intc.GetIcIp(), intc.GetIcmr(), intc.GetIcpr(),
                    c.regs[14]);
            });

            tm.OnPc(0x800AA968u, [](const TraceContext& c) {
                const int n = nop1_fires.fetch_add(1) + 1;
                if (n > kMaxFiresPerSite) return;
                auto& intc = c.emu.Get<Sa1110Intc>();
                /* GEDR snapshot: bit-11 sub-dispatch ran out of recognized
                   GPIO sub-bits. GEDR is at PA 0x90040018 (VA 0xA9040018). */
                const auto gedr = c.ReadVa32(0xA9040018u);
                LOG(Trace, "[OEMIH] NOP1-FALLTHROUGH #%d  ICIP=0x%08X "
                           "ICMR=0x%08X ICPR=0x%08X GEDR=%s\n",
                    n, intc.GetIcIp(), intc.GetIcmr(), intc.GetIcpr(),
                    gedr ? ([](uint32_t v) {
                        static char buf[16];
                        snprintf(buf, sizeof(buf), "0x%08X", v);
                        return buf;
                    })(*gedr) : "<not-fast-mapped>");
            });

            static std::atomic<int> tpe_fires{0};
            static std::atomic<int> setup_fires{0};
            static std::atomic<int> sampler_fires{0};

            tm.OnPc(0x00F33E30u, [](const TraceContext& c) {
                const int n = tpe_fires.fetch_add(1) + 1;
                if (n > kMaxFiresPerSite) return;
                LOG(Trace, "[TOUCH.DLL] TouchPanelEnable #%d  "
                           "R0=0x%08X LR=0x%08X CPSR=0x%08X\n",
                    n, c.regs[0], c.regs[14], c.cpsr);
            });

            tm.OnPc(0x00F312D0u, [](const TraceContext& c) {
                const int n = setup_fires.fetch_add(1) + 1;
                if (n > kMaxFiresPerSite) return;
                LOG(Trace, "[TOUCH.DLL] sub_F312D0 (setup/VirtualCopy) "
                           "#%d  LR=0x%08X CPSR=0x%08X\n",
                    n, c.regs[14], c.cpsr);
            });

            tm.OnPc(0x00F34054u, [](const TraceContext& c) {
                const int n = sampler_fires.fetch_add(1) + 1;
                if (n > kMaxFiresPerSite) return;
                LOG(Trace, "[TOUCH.DLL] StartAddr (sampler thread) "
                           "#%d  LR=0x%08X CPSR=0x%08X\n",
                    n, c.regs[14], c.cpsr);
            });

            /* OEMIH single return epilogue at 0x800AABB4 (POP {R4-R7,
               R10,R11,PC}). R0 at this PC = SYSINTR returned to nk
               IRQ dispatcher. Buckets per distinct R0 to see which
               SYSINTRs dominate the stuck-phase IRQ traffic. */
            tm.OnPc(0x800AABB4u, [](const TraceContext& c) {
                const uint32_t sysintr = c.regs[0];
                static std::atomic<int> bucket_count[64] = {};
                const int bucket = sysintr < 64 ? (int)sysintr : 63;
                const int n = bucket_count[bucket].fetch_add(1) + 1;
                if (n <= 5 || n == 100 || n == 1000 || n == 10000
                           || n == 100000 || n == 1000000) {
                    LOG(Trace, "[OEMIH] return SYSINTR=%u count=%d\n",
                        sysintr, n);
                }
            });

            tm.OnRunLoopIter([](const TraceContext& c) {
                static uint32_t last_w = 0, last_h = 0;
                auto& lcd = c.emu.Get<Sa1110Lcd>();
                const uint32_t w = lcd.GetGuestW();
                const uint32_t h = lcd.GetGuestH();
                if (w == last_w && h == last_h) return;
                last_w = w; last_h = h;
                LOG(Trace, "[LCD-DIMS] GuestW=%u GuestH=%u "
                           "Enabled=%d Color=%d FbPa=0x%08X\n",
                    w, h, lcd.IsEnabled() ? 1 : 0,
                    lcd.IsColor() ? 1 : 0, lcd.GetFbPa());
            });
        });
    }
};

REGISTER_SERVICE(IpaqOemihNop3Probe);

}  /* namespace */

#endif  /* CERF_DEV_MODE */
