#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_cpu.h"
#include "../../jit/arm_cpu_ops.h"
#include "../../jit/cpu_state.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "bundle.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#if CERF_DEV_MODE

/* Host-thread sampler of the wedged JIT thread (OnPc/OnRunLoopIter can't see a
   Run() that never returns): frozen guest PC, CPSR.I, irq pending, OST/INTC. */
namespace {

class TraceNecParkWatchdog : public Service {
public:
    using Service::Service;

    void OnShutdown() override {
        stop_.store(true, std::memory_order_release);
        if (watchdog_.joinable()) watchdog_.join();
    }

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&] {
            watchdog_ = std::thread([this] { PollLoop(); });
        });
    }

private:
    std::thread       watchdog_;
    std::atomic<bool> stop_{false};

    void PollLoop() {
        auto*    st         = emu_.Get<ArmCpu>().State();
        auto&    pd         = emu_.Get<PeripheralDispatcher>();
        uint32_t last_cyc   = 0;
        uint32_t last_pc    = 0;
        uint32_t stuck_runs = 0;

        while (!stop_.load(std::memory_order_acquire)) {
            for (int i = 0; i < 4 && !stop_.load(std::memory_order_acquire); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (stop_.load(std::memory_order_acquire)) break;

            const uint32_t pc   = st->gprs[15];
            const uint32_t cpsr = ArmCpuGetCpsrWithFlags(st);
            const uint32_t pend = st->irq_interrupt_pending;
            const uint32_t cyc  = st->guest_cycle_counter;
            const bool     frozen = (cyc == last_cyc);

            LOG(Trace, "[WATCHDOG] pc=0x%08X cpsr=0x%08X I=%u pend=%u cycles=0x%08X "
                       "%s (pc_prev=0x%08X)\n",
                pc, cpsr, (cpsr >> 7) & 1u, pend, cyc,
                frozen ? "FROZEN" : "advancing", last_pc);

            if (frozen) {
                ++stuck_runs;
                if (stuck_runs <= 4) {
                    const uint32_t oscr  = pd.ReadWord(0x40A00010u);
                    const uint32_t ossr  = pd.ReadWord(0x40A00014u);
                    const uint32_t oier  = pd.ReadWord(0x40A0001Cu);
                    const uint32_t osmr0 = pd.ReadWord(0x40A00000u);
                    const uint32_t icip  = pd.ReadWord(0x40D00000u);
                    const uint32_t icmr  = pd.ReadWord(0x40D00004u);
                    const uint32_t icpr  = pd.ReadWord(0x40D00010u);
                    LOG(Trace, "[WATCHDOG] OST osmr0=0x%08X oscr=0x%08X ossr=0x%X "
                               "oier=0x%X armed=%d | INTC icip=0x%08X icmr=0x%08X "
                               "icpr=0x%08X ost26{pend=%d masked=%d ip=%d}\n",
                        osmr0, oscr, ossr, oier, (oier & ~ossr & 0xFu) != 0,
                        icip, icmr, icpr, (icpr >> 26) & 1u,
                        ((icmr >> 26) & 1u) == 0, (icip >> 26) & 1u);
                }
            } else {
                stuck_runs = 0;
            }
            last_cyc = cyc;
            last_pc  = pc;
        }
    }
};

REGISTER_SERVICE(TraceNecParkWatchdog);

}  /* namespace */

#endif  /* CERF_DEV_MODE */
