#include "../../core/service.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_jit.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../jit/arm/arm_mmu_state.h"
#include "../../jit/arm/cpu_state.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <atomic>
#include <chrono>
#include <thread>

#if CERF_DEV_MODE

namespace {

/* Names the deep-sleep resume hang: when the guest stops advancing (guest PC and
   OST OSCR both frozen) no TraceManager hook fires (Run() never returns), so a
   host-thread sampler is the only way to capture the stuck PC + timer/INTC state.
   Falcon-only, observation-only - reads guest state, never writes it. */
class FalconResumeStallWatchdog : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::FalconPC3xx;
    }

    void OnReady() override {
        thread_ = std::thread([this] { Loop(); });
    }

    void OnShutdown() override {
        stop_.store(true, std::memory_order_release);
        if (thread_.joinable()) thread_.join();
    }

private:
    void Loop() {
        auto&        d   = emu_.Get<PeripheralDispatcher>();
        ArmCpuState* cpu = emu_.Get<ArmJit>().CpuState();
        ArmMmuState* mmu = emu_.Get<ArmMmu>().State();

        uint32_t last_pc      = 0;
        uint32_t last_oscr    = 0;
        int      still        = 0;
        bool     reported     = false;
        uint64_t osc_samples  = 0;

        /* PXA255 Power Manager (base 0x40F00000): PSPR +0x08 is the sleep resume
           vector the boot ROM jumps to on an SMR wake; PSSR +0x04 status; RCSR
           +0x30 reset cause. The deep-sleep resume fix needs the PSPR value the OS
           leaves at sleep time, so log every change across boot -> sleep -> wake. */
        uint32_t last_pspr = 0xFFFFFFFFu;
        uint32_t last_rcsr = 0xFFFFFFFFu;
        uint32_t last_pssr = 0xFFFFFFFFu;

        while (!stop_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            const uint32_t pspr = d.ReadWord(0x40F00008u);
            const uint32_t rcsr = d.ReadWord(0x40F00030u);
            const uint32_t pssr = d.ReadWord(0x40F00004u);
            if (pspr != last_pspr || rcsr != last_rcsr || pssr != last_pssr) {
                LOG(SocReset, "[PMWATCH] PSPR=0x%08X PSSR=0x%08X RCSR=0x%08X "
                    "deep_sleep=%u reset_pending=%u\n",
                    pspr, pssr, rcsr, cpu->deep_sleep, cpu->reset_pending);
                last_pspr = pspr;
                last_rcsr = rcsr;
                last_pssr = pssr;
            }

            const uint32_t pc   = cpu->gprs[15];
            const uint32_t oscr = d.ReadWord(0x40A00010u);   /* OST OSCR (icount) */

            /* OSCR advance per real second: nominal 3.6864 MHz. Log ~1 Hz so the
               guest-time/real-time ratio (and any sleep-resume jump) is visible. */
            if (++osc_samples % 5u == 0u) {
                LOG(SocReset, "[OSTRATE] OSCR=0x%08X OSMR0=0x%08X (period=%d) "
                    "RCNR=0x%08X RTSR=0x%08X deep_sleep=%u\n",
                    oscr, d.ReadWord(0x40A00000u),
                    static_cast<int32_t>(d.ReadWord(0x40A00000u) - oscr),
                    d.ReadWord(0x40900000u), d.ReadWord(0x40900008u),
                    cpu->deep_sleep);
            }

            if (pc == last_pc && oscr == last_oscr) {
                if (++still == 5 && !reported) {              /* ~1 s frozen */
                    reported = true;
                    LOG(SocReset,
                        "[STALL] guest frozen: PC=0x%08X CPSR.I=%u deep_sleep=%u "
                        "irqp=%u reset_pending=%u | OSCR=0x%08X OSMR0=0x%08X "
                        "OSSR=0x%X OIER=0x%X | ICIP=0x%08X ICMR=0x%08X ICPR=0x%08X "
                        "| PSPR=0x%08X PSSR=0x%08X RCSR=0x%08X "
                        "| SCTLR=0x%08X M=%u V=%u pid=0x%08X\n",
                        pc, cpu->cpsr.bits.irq_disable, cpu->deep_sleep,
                        cpu->irq_interrupt_pending, cpu->reset_pending,
                        oscr, d.ReadWord(0x40A00000u), d.ReadWord(0x40A00014u),
                        d.ReadWord(0x40A0001Cu), d.ReadWord(0x40D00000u),
                        d.ReadWord(0x40D00004u), d.ReadWord(0x40D00010u),
                        d.ReadWord(0x40F00008u), d.ReadWord(0x40F00004u),
                        d.ReadWord(0x40F00030u),
                        mmu->control_register.word, mmu->control_register.bits.m,
                        mmu->control_register.bits.v, mmu->process_id);
                }
            } else {
                if (reported)
                    LOG(SocReset, "[STALL] recovered: PC=0x%08X OSCR=0x%08X\n",
                        pc, oscr);
                still    = 0;
                reported = false;
            }
            last_pc   = pc;
            last_oscr = oscr;
        }
    }

    std::thread       thread_;
    std::atomic<bool> stop_{false};
};

}  /* namespace */

REGISTER_SERVICE(FalconResumeStallWatchdog);

#endif  /* CERF_DEV_MODE */
