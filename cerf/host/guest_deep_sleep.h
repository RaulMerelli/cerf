#pragma once

#include "../core/service.h"

#include <atomic>
#include <cstdint>

/* Deep-sleep recovery: a SoC power-down register write (e.g. SA-1110 PMCR.SF)
   calls Enter(), which halts the CPU and shows a no-timeout "Shut down CERF?"
   prompt - Cancel wakes via a sleep-mode reset (GuestCpuReset::SleepWakeReset),
   OK exits/saves. Generic; each SoC supplies the sleep cause via ResetCauseLatch. */
/* Implemented by the SoC peripheral owning the sleep-wake reset cause (SA-1110
   RCSR.SMR, PXA255 RCSR.SMR). Non-Service so a Peripheral can implement it; the
   implementer self-registers with GuestDeepSleep from its OnReady. */
class DeepSleepWaker {
public:
    virtual ~DeepSleepWaker() = default;
    /* Latch the sleep-mode reset cause so the OAL boot path resumes on wake. */
    virtual void LatchSleepWakeCause() = 0;
    /* Clear it on a non-resume reset. CERF's resume jumps straight to the saved
       vector, skipping the OAL/EBOOT code that clears this cause on real HW, so a
       later reboot would re-read the stale cause and resume from a now-invalid
       save block. Cleared on every SetResetPending(is_resume=false). */
    virtual void ClearSleepWakeCause() = 0;
};

/* pc == 0 ⇒ no resume; the wake falls through to the cold entry. restore_mmu
   true ⇒ the wake reinstates cp15 c1/c2/c3 so the guest resumes with the MMU on
   (DevEmu EBOOT); false ⇒ the resume entry runs MMU-off and re-enables it itself
   (simpad kernel `start`). */
struct SleepResumeState {
    uint32_t pc          = 0;
    bool     restore_mmu = false;
    uint32_t mmu_control = 0;
    uint32_t ttbr0       = 0;
    uint32_t dacr        = 0;
};

/* A board whose boot ROM resumes the guest at a saved vector on a sleep wake
   (simpad: the PSPR vector its kernel `start` stores; DevEmu: the SLEEPDATA
   WakeAddr EBOOT jumps to). Self-registers with GuestDeepSleep from OnReady;
   absent → the guest resumes at the cold entry. */
class SleepResumeVectorProvider {
public:
    virtual ~SleepResumeVectorProvider() = default;
    virtual SleepResumeState Resume() = 0;
};

class GuestDeepSleep : public Service {
public:
    using Service::Service;

    /* OnReady-time only; at most one waker per emulator instance. */
    void RegisterWaker(DeepSleepWaker* waker);

    /* Any thread. Clears the latched sleep-wake cause; called from a non-resume
       reset so the rebooting guest doesn't misread it as another sleep wake. */
    void ClearWakeCause();

    /* OnReady-time, board-scoped: supplies the sleep-wake resume vector. */
    void RegisterResumeVectorProvider(SleepResumeVectorProvider* p);

    /* JIT thread. Halt the CPU for deep sleep and post the recovery prompt. */
    void Enter();

    /* Hibernation worker, after a full restore: a machine saved mid-deep-sleep
       comes back with deep_sleep set, so auto-wake it (the dialog's Cancel
       action) - otherwise the JIT parks forever at "State restored". */
    void OnFullRestore();

private:
    void Recover();      /* UI thread: run the prompt and act on the choice. */
    void DeliverWake();  /* latch the wake cause and reset to the resume vector. */

    DeepSleepWaker*            waker_                  = nullptr;
    SleepResumeVectorProvider* resume_vector_provider_ = nullptr;
    std::atomic<bool>          active_{false};
};
