#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"
#include "bundle.h"

#include <atomic>
#include <memory>
#include <string>

#if CERF_DEV_MODE

namespace {

/* Filter to device.exe's FCSE PID (slot 4, observed in the launch trace): these
   are slot-0 user VAs, so without the filter another process executing the same
   VA misattributes device.exe's init progress. */
constexpr uint32_t kDeviceExePid = 0x08000000u;

class SimpadSl4DeviceInitProgress : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            Mark(tm, 0x13934u, "branchA: load drivers (sub_15E8C)");
            Mark(tm, 0x1395Cu, "branchA: WaitForSingleObject(SYSTEM/BootPhase2)");
            Mark(tm, 0x1398Cu, "branchB: reached (Start DevMgr absent)");
            Mark(tm, 0x13994u, "branchB: load drivers (sub_15E8C)");
            Mark(tm, 0x139A0u, "branchB: SignalStarted");
            Mark(tm, 0x139A4u, "init done -> main loop (stage 3)");
            Mark(tm, 0x139B4u, "main loop WaitForSingleObject");

            /* LoadDriver runs each driver's Init synchronously; the last key
               before the park is the driver whose Init never returns. */
            tm.OnPcFiltered(0x16004u, IsDeviceExe, [](const TraceContext& c) {
                LOG(Trace, "[DEVINIT] LoadDriver key=\"%s\" r0=0x%08X lr=0x%08X\n",
                    ReadWide(c, c.regs[0]).c_str(), c.regs[0], c.regs[14]);
            });
            auto wn = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPcFiltered(0x16028u, IsDeviceExe, [wn](const TraceContext& c) {
                if (wn->fetch_add(1) >= 80u) return;
                LOG(Trace, "[DEVINIT] WaitForSingleObject h=0x%08X ms=0x%08X "
                           "lr=0x%08X\n", c.regs[0], c.regs[1], c.regs[14]);
            });
        });
    }

private:
    static bool IsDeviceExe(const TraceContext& c) {
        return c.emu.Get<ArmMmu>().State()->process_id == kDeviceExePid;
    }
    static std::string ReadWide(const TraceContext& c, uint32_t base) {
        std::string s;
        if (!base) return s;
        for (uint32_t i = 0; i < 260u; ++i) {
            auto w = c.ReadVa16(base + 2u * i);
            if (!w || *w == 0) break;
            if (*w >= 0x20 && *w < 0x7F) s.push_back(char(*w));
        }
        return s;
    }
    void Mark(TraceManager& tm, uint32_t va, const char* tag) {
        auto fired = std::make_shared<std::atomic<bool>>(false);
        tm.OnPcFiltered(va, IsDeviceExe, [fired, tag](const TraceContext& c) {
            if (fired->exchange(true)) return;
            LOG(Trace, "[DEVINIT] %s pc=0x%08X lr=0x%08X\n", tag, c.pc, c.regs[14]);
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4DeviceInitProgress);

#endif  // CERF_DEV_MODE
