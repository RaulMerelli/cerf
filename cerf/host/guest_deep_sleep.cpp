#include "guest_deep_sleep.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../jit/guest_engine.h"
#include "../state/shutdown_dialog.h"
#include "guest_power_notifier.h"
#include "host_window.h"

REGISTER_SERVICE(GuestDeepSleep);

void GuestDeepSleep::RegisterWaker(DeepSleepWaker* waker) {
    waker_ = waker;
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

void GuestDeepSleep::DeliverWake() {
    waker_->LatchSleepWakeCause();
    if (resume_vector_provider_) resume_vector_provider_->ApplyPendingResume();
    emu_.Get<GuestEngine>().SetResetPending(/*is_resume=*/true);
}

void GuestDeepSleep::OnFullRestore() {
    /* A machine restored mid-deep-sleep wakes the same way Cancel does, no prompt. */
    if (emu_.Get<GuestEngine>().DeepSleep()) DeliverWake();
}

void GuestDeepSleep::Recover() {
    const ShutdownChoice c =
        emu_.Get<ShutdownDialog>().Show(ShutdownTrigger::DeepSleep);
    LOG(SocReset, "[DEEPSLEEP] Recover: dialog choice=%d (0=Cancel/wake)\n",
        static_cast<int>(c));
    active_.store(false);
    if (c == ShutdownChoice::Cancel) {
        DeliverWake();                           /* wake = sleep-mode reset */
        return;
    }
    emu_.Get<HostWindow>().PerformShutdownChoice(c);
}
