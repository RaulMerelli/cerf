#include "guest_deep_sleep.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../jit/guest_engine.h"
#include "../state/shutdown_dialog.h"
#include "guest_power_notifier.h"
#include "host_window.h"

#include <chrono>

REGISTER_SERVICE(GuestDeepSleep);

namespace {
constexpr int kResumeGraceMs = 100;
}

void GuestDeepSleep::RegisterWaker(DeepSleepWaker* waker) {
    if (clock_stop_ != nullptr) {
        LOG(Caution, "GuestDeepSleep: a DeepSleepWaker and a DeepSleepClockStop are both "
                "registered - sleep-exit is one shape or the other, so one SoC's wake "
                "model is wrong\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    waker_ = waker;
}

void GuestDeepSleep::RegisterClockStopWaker(DeepSleepClockStop* waker) {
    if (waker_ != nullptr) {
        LOG(Caution, "GuestDeepSleep: a DeepSleepClockStop and a DeepSleepWaker are both "
                "registered - sleep-exit is one shape or the other, so one SoC's wake "
                "model is wrong\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    clock_stop_ = waker;
}

void GuestDeepSleep::RegisterPowerUpListener(std::function<void()> fn) {
    power_up_listeners_.push_back(std::move(fn));
}

void GuestDeepSleep::ClearWakeCause() {
    if (waker_) waker_->ClearSleepWakeCause();
}

void GuestDeepSleep::RegisterResumeVectorProvider(SleepResumeVectorProvider* p) {
    resume_vector_provider_ = p;
}

void GuestDeepSleep::Enter() {
    /* A pending reset is a wake/reboot in flight, and the wake itself is a reset;
       entering sleep here lets the woken guest re-execute its sleep write before
       the poll delivers the reset, posting a spurious second recovery prompt and
       hanging. Sleep entry yields to a pending reset. */
    if (emu_.Get<GuestEngine>().ResetPending()) {
        LOG(SocReset, "[DEEPSLEEP] Enter: reset pending, skip (wake/reboot wins)\n");
        return;
    }
    if (active_.exchange(true)) {
        LOG(SocReset, "[DEEPSLEEP] Enter: already active, skip\n");
        return;   /* one prompt per sleep */
    }
    LOG(SocReset, "[DEEPSLEEP] Enter: sleep begin\n");
    emu_.Get<GuestPowerNotifier>().NotifyPowerDown();
    emu_.Get<GuestEngine>().EnterDeepSleep();
    emu_.Get<HostWindow>().RunOnUiThread([this] { Recover(); });
}

void GuestDeepSleep::RequestHardwareWake() {
    emu_.Get<GuestEngine>().SetResetPending(/*is_resume=*/true);
    TearDownPromptForHardwareWake();
    emu_.Get<GuestPowerNotifier>().NotifyResume(ResumeSource::Hardware);
}

void GuestDeepSleep::TearDownPromptForHardwareWake() {
    if (!active_.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lk(resume_mtx_);
        hw_resumed_.store(true, std::memory_order_release);
    }
    resume_cv_.notify_all();
    emu_.Get<HostWindow>().RunOnUiThread(
        [this] { emu_.Get<ShutdownDialog>().DismissAsCancel(); });
}

void GuestDeepSleep::DeliverWake() {
    if (clock_stop_) {
        clock_stop_->OnPowerUp();
        for (auto& fn : power_up_listeners_) fn();
        emu_.Get<GuestPowerNotifier>().NotifyResume(ResumeSource::User);
        emu_.Get<GuestEngine>().ExitDeepSleep();
        return;
    }
    waker_->LatchSleepWakeCause();
    if (resume_vector_provider_) resume_vector_provider_->ApplyPendingResume();
    emu_.Get<GuestEngine>().SetResetPending(/*is_resume=*/true);
    emu_.Get<GuestPowerNotifier>().NotifyResume(ResumeSource::User);
}

void GuestDeepSleep::OnFullRestore() {
    /* A machine restored mid-deep-sleep wakes the same way Cancel does, no prompt. */
    if (emu_.Get<GuestEngine>().DeepSleep()) DeliverWake();
}

void GuestDeepSleep::Recover() {
    {
        std::unique_lock<std::mutex> lk(resume_mtx_);
        resume_cv_.wait_for(lk, std::chrono::milliseconds(kResumeGraceMs),
                            [this] { return hw_resumed_.load(std::memory_order_acquire); });
    }
    if (hw_resumed_.exchange(false)) {
        active_.store(false);
        return;
    }
    const ShutdownChoice c =
        emu_.Get<ShutdownDialog>().Show(ShutdownTrigger::DeepSleep);
    LOG(SocReset, "[DEEPSLEEP] Recover: dialog choice=%d (0=Cancel/wake)\n",
        static_cast<int>(c));
    const bool hw_resumed = hw_resumed_.exchange(false);
    active_.store(false);
    if (c == ShutdownChoice::Cancel) {
        if (!hw_resumed) DeliverWake();          /* wake = sleep-mode reset */
        return;
    }
    emu_.Get<HostWindow>().PerformShutdownChoice(c);
}
