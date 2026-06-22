#include "guest_cpu_reset.h"

#include "../boot/guest_cold_boot.h"
#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../jit/arm/arm_jit.h"

REGISTER_SERVICE(GuestCpuReset);

void GuestCpuReset::SetCauseLatch(ResetCauseLatch* latch) {
    if (latch_ != nullptr) {
        LOG(Caution, "GuestCpuReset: second ResetCauseLatch registered - "
                "one SoC owns the reset-cause register; two implementers "
                "mean a ShouldRegister gate is wrong\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    latch_ = latch;
}

void GuestCpuReset::WarmReset() {
    if (latch_) latch_->LatchWarmReset();
    emu_.Get<ArmJit>().SetResetPending();
}

void GuestCpuReset::ColdReset() {
    if (latch_) latch_->LatchColdReset();
    emu_.Get<ArmJit>().SetResetPending();
}

void GuestCpuReset::WatchdogReset() {
    if (latch_) latch_->LatchWatchdogReset();
    emu_.Get<ArmJit>().SetResetPending();
}

void GuestCpuReset::RegisterResetListener(std::function<void()> fn) {
    reset_listeners_.push_back(std::move(fn));
}

void GuestCpuReset::OnResetDelivered() {
    for (auto& fn : reset_listeners_) fn();
    emu_.Get<GuestColdBoot>().ExecuteIfPending();
}
