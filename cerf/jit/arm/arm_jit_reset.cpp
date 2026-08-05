#include "arm_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../host/guest_deep_sleep.h"
#include "../../host/guest_power_notifier.h"
#include "../../socs/guest_cpu_reset.h"
#include "cpu_state.h"

void ArmJit::SetResetPending(bool is_resume) {
    emu_.Get<GuestCpuReset>().SetPendingResume(is_resume);
    cpu_state_->reset_pending = 1u;
    SetEvent(idle_event_);
    if (is_resume) return;
    emu_.Get<GuestDeepSleep>().ClearWakeCause();
    emu_.Get<GuestPowerNotifier>().NotifyReboot();
}
