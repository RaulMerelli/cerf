#pragma once

#include "../core/service.h"

#include <functional>
#include <vector>

/* Implemented by the SoC peripheral owning the reset-cause register
   (e.g. SA-1110 RCSR). Non-Service so a Peripheral can implement it
   without a Service diamond; the implementer self-registers with
   GuestCpuReset from its OnReady. */
class ResetCauseLatch {
public:
    virtual ~ResetCauseLatch() = default;
    virtual void LatchWarmReset()     = 0;   /* CPU reset, RAM preserved */
    virtual void LatchColdReset()     = 0;   /* CPU reset, RAM lost */
    virtual void LatchWatchdogReset() = 0;   /* watchdog expiry */
};

/* Routes CERF-initiated CPU resets through the SoC's reset-cause latch
   before pending the reset. On cause-tracking SoCs a causeless reset
   hangs the guest: the SA-1110 PPC2002 boot path reads RCSR==0 as
   "sleep exit" and resumes from a stale save block into an UND loop. */
class GuestCpuReset : public Service {
public:
    using Service::Service;

    /* OnReady-time only; at most one latch per emulator instance. */
    void SetCauseLatch(ResetCauseLatch* latch);

    /* Any thread. Host soft reset: RAM survives the reset. */
    void WarmReset();

    /* Any thread. Host hard reset: caller wipes RAM at delivery. */
    void ColdReset();

    /* Any thread. SoC watchdog expiry (e.g. OSMR3 match with OWER.WME=1). */
    void WatchdogReset();

    /* OnReady-time only. Listeners model devices wired to the board's
       reset line (RESET_OUT): they run at reset delivery on the JIT
       thread, for every delivered reset regardless of source. */
    void RegisterResetListener(std::function<void()> fn);

    /* JIT thread, reset-delivery branch only: runs the reset-line
       listeners, then an armed GuestColdBoot hard reset. */
    void OnResetDelivered();

private:
    ResetCauseLatch*                   latch_ = nullptr;
    std::vector<std::function<void()>> reset_listeners_;
};
