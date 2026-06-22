#include "../trace_manager.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_jit.h"
#include "../../jit/arm/cpu_state.h"
#include "../../peripherals/mediaq_mq1188/mediaq_mq1188.h"
#include "../../peripherals/peripheral_dispatcher.h"

#if CERF_DEV_MODE

namespace {

/* Logs the GE blit-dest registers (GE0AR/GE0BR/GE01R/GE02R) and the display
   scanout params (FbWindowOffset/Stride/Bpp) on each deep-sleep phase change,
   so a pre-sleep vs post-resume diff names what diverges. Observation-only.
   MediaQ base 0x08000000 + 0x40000 reg window; GE reg block at chip +0x200. */
class FalconMediaqResumeRegs : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::FalconPC3xx;
    }

    void OnReady() override {
        emu_.Get<TraceManager>().OnRunLoopIter(
            [this](const TraceContext& c) { Sample(c); });
    }

private:
    enum Phase { kPreSleep, kAsleep, kResumed };

    void Sample(const TraceContext& c) {
        if (!cpu_) cpu_ = c.emu.Get<ArmJit>().CpuState();
        const uint32_t ds = cpu_->deep_sleep;

        const Phase prev = phase_;
        if (ds && phase_ != kAsleep)            phase_ = kAsleep;
        else if (!ds && phase_ == kAsleep)    { phase_ = kResumed; ++resume_n_; }
        const bool transition = (phase_ != prev);

        /* Sample on every phase transition and otherwise periodically; the cheap
           per-iter cost is one CpuState read + a counter. */
        if (!transition && (++throttle_ % 2000u) != 0u) return;

        auto& d  = c.emu.Get<PeripheralDispatcher>();
        auto& mq = c.emu.Get<MediaQMq1188>();

        /* GE dst registers (raw - the per-part base/stride masks live in the GE
           concrete; log raw so the cross-boundary diff is unambiguous). */
        /* Read GE regs via the queued alias (0x1400) not the direct block (0x200):
           the direct block is the SD/MMC controller region, and reading it here
           would route through that path. The queued alias returns the same GE. */
        const uint32_t ge01 = d.ReadWord(0x08041404u);  /* GE01R last blit size W|H */
        const uint32_t ge02 = d.ReadWord(0x08041408u);  /* GE02R last blit dst XY   */
        const uint32_t ge0a = d.ReadWord(0x08041428u);  /* GE0AR dst stride + bpp   */
        const uint32_t ge0b = d.ReadWord(0x0804142Cu);  /* GE0BR dst base           */

        const uint64_t sig =
            static_cast<uint64_t>(ge0a) ^ (static_cast<uint64_t>(ge0b) << 18) ^
            (static_cast<uint64_t>(ge01) << 36) ^ (static_cast<uint64_t>(ge02) << 50) ^
            (static_cast<uint64_t>(mq.FbWindowOffset()) << 4) ^
            (static_cast<uint64_t>(mq.Stride()) << 22) ^
            (static_cast<uint64_t>(mq.Bpp()) << 40);
        if (!transition && sig == last_sig_) return;
        last_sig_ = sig;

        const char* pn = phase_ == kPreSleep ? "PRE"
                       : phase_ == kAsleep   ? "SLEEP" : "RESUMED";
        LOG(SocReset,
            "[MQREGS] phase=%s rN=%u%s | DISP base=0x%05X stride=%u bpp=%u en=%u "
            "%ux%u | GE0A=0x%08X GE0B=0x%08X (bpp=%u) GE01=0x%08X GE02=0x%08X\n",
            pn, resume_n_, transition ? " *TRANS*" : "",
            mq.FbWindowOffset(), mq.Stride(), mq.Bpp(),
            mq.IsEnabled() ? 1u : 0u, mq.GetGuestW(), mq.GetGuestH(),
            ge0a, ge0b, (ge0a >> 30) & 3u, ge01, ge02);
    }

    ArmCpuState* cpu_      = nullptr;
    Phase        phase_    = kPreSleep;
    uint32_t     resume_n_ = 0;
    uint32_t     throttle_ = 0;
    uint64_t     last_sig_ = ~0ull;
};

}  /* namespace */

REGISTER_SERVICE(FalconMediaqResumeRegs);

#endif  /* CERF_DEV_MODE */
