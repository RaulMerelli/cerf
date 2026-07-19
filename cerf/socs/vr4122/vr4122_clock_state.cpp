#include "vr4122_clock_state.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

REGISTER_SERVICE(Vr4122ClockState);

bool Vr4122ClockState::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSoc() == SocFamily::VR4122;
}

void Vr4122ClockState::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        /* VR4131 UM U15350EJ2V0UM 12.2.6: PMUTCLKDIVREG is cleared to 0 at RTC reset, and a
           set value "becomes valid after a reset other than an RTC reset occurs". */
        if (kind == ResetLineKind::Rtc) { pending_ = 0; active_ = 0; }
        else                            { active_ = pending_; }
    });
}

void Vr4122ClockState::SetPending(uint16_t tclkdiv) { pending_ = tclkdiv; }

void Vr4122ClockState::SaveState(StateWriter& w) const {
    w.Write(pending_);
    w.Write(active_);
}
void Vr4122ClockState::RestoreState(StateReader& r) {
    r.Read(pending_);
    r.Read(active_);
}
