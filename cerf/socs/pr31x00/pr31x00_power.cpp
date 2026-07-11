#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../host/guest_deep_sleep.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <atomic>
#include <cstdint>

namespace {

/* PR31x00 Power module. The single Power Control Register sits at offset $1C4 of
   the Internal Function Registers (PA 0x10C00000, TMPR3911/3912 Table 4.2.1);
   field layout and reset values per §12.3.1. */
constexpr uint32_t kBase = 0x10C001C4u;
constexpr uint32_t kSize = 0x4u;

constexpr uint32_t kOnButn    = 1u << 31;   /* R: ONBUTN signal status */
constexpr uint32_t kPwrInt    = 1u << 30;   /* R: PWRINT signal status */
constexpr uint32_t kPwrOk     = 1u << 29;   /* R: PWROK signal status */
constexpr uint32_t kForceShutDwn = 1u << 9;
constexpr uint32_t kColdStart = 1u << 2;    /* set by RESET: a power-on reset occurred */
constexpr uint32_t kPwrCs     = 1u << 1;
constexpr uint32_t kVccOn     = 1u << 0;

constexpr uint32_t kWritable = 0x1E00FFBFu;   /* VIDRF..DIVMOD, STPTIMERVAL..SELC2MS, BPDBVCC3..VCCON */
constexpr uint32_t kReadOnly = kOnButn | kPwrInt | kPwrOk;

constexpr uint32_t kCauseNone = 0;
constexpr uint32_t kCauseWarm = 1;
constexpr uint32_t kCauseCold = 2;

class Pr31x00Power : public Peripheral, public ResetCauseLatch, public DeepSleepClockStop {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::PR31500 || soc == SocFamily::PR31700;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        auto& reset = emu_.Get<GuestCpuReset>();
        reset.SetCauseLatch(this);
        reset.RegisterResetListener([this] { ApplyResetCause(); });
        emu_.Get<GuestDeepSleep>().RegisterClockStopWaker(this);
    }

    /* PWRCS and VCCON are "set whenever the ONBUTN signal is asserted or an enabled
       interrupt occurs while the PWROK signal is high", and "FORCESHUTDWN ... is set
       whenever the VCCON bit and the PWRCS bit are set by either the ONBUTN signal
       assertion or an enabled interrupt occurrence" (§12.3.1, p.12-13/12-14). */
    void OnPowerUp() override {
        ctl_.fetch_or(kPwrCs | kVccOn | kForceShutDwn, std::memory_order_acq_rel);
    }

    /* "COLDSTART: This bit is set by RESET" (§12.3.1, p.12-14). */
    void LatchColdReset() override { pending_cause_.store(kCauseCold, std::memory_order_release); }
    void LatchWarmReset() override { pending_cause_.store(kCauseWarm, std::memory_order_release); }

    /* No PR31x00 watchdog is modelled, so nothing raises this cause. */
    void LatchWatchdogReset() override {
        HaltUnsupportedAccess("PR31x00 Power watchdog reset cause", kBase, Ctl());
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        if (addr != kBase) {
            HaltUnsupportedAccess("PR31x00 Power ReadWord", addr, 0);
        }
        return Ctl() | signals_;
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr != kBase) {
            HaltUnsupportedAccess("PR31x00 Power WriteWord", addr, value);
        }
        const uint32_t prev = Ctl();
        const uint32_t next = value & kWritable;
        ctl_.store(next, std::memory_order_release);

        /* "When powering down the system, [VCCON] must be cleared simultaneously
           with the PWRCS bit" (§12.3.1) - the guest turning the machine off. */
        const bool was_on = (prev & (kVccOn | kPwrCs)) != 0u;
        const bool now_off = (next & (kVccOn | kPwrCs)) == 0u;
        if (was_on && now_off) {
            emu_.Get<GuestDeepSleep>().Enter();
        }
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("PR31x00 Power ReadByte", addr, 0); }
    uint16_t ReadHalf (uint32_t addr) override { HaltUnsupportedAccess("PR31x00 Power ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 Power WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 Power WriteHalf", addr, v); }

    void SaveState(StateWriter& w) override {
        w.Write(Ctl()); w.Write(signals_);
        w.Write(pending_cause_.load(std::memory_order_acquire));
    }
    void RestoreState(StateReader& r) override {
        uint32_t ctl = 0;
        r.Read(ctl);
        ctl_.store(ctl, std::memory_order_release);
        r.Read(signals_);
        uint32_t cause = kCauseNone;
        r.Read(cause);
        pending_cause_.store(cause, std::memory_order_release);
    }

private:
    uint32_t Ctl() const { return ctl_.load(std::memory_order_acquire); }

    void ApplyResetCause() {
        switch (pending_cause_.exchange(kCauseNone, std::memory_order_acq_rel)) {
            case kCauseCold: ctl_.fetch_or(kColdStart, std::memory_order_acq_rel);   return;
            case kCauseWarm: ctl_.fetch_and(~kColdStart, std::memory_order_acq_rel); return;
            default:
                HaltUnsupportedAccess("PR31x00 Power reset delivered with no cause latched",
                                      kBase, Ctl());
        }
    }

    std::atomic<uint32_t> ctl_{kColdStart};

    uint32_t signals_ = kPwrOk;

    std::atomic<uint32_t> pending_cause_{kCauseNone};

    static_assert((kWritable & kReadOnly) == 0u, "writable and read-only fields overlap");
};

}  /* namespace */

REGISTER_SERVICE(Pr31x00Power);
