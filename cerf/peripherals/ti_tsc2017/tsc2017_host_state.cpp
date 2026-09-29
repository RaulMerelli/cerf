#include "tsc2017_host_state.h"

#include "../../core/cerf_emulator.h"
#include "ti_tsc2017_wiring.h"

REGISTER_SERVICE(Tsc2017HostState);

namespace {

/* TSC2017 datasheet SBAS472 Equation 1 and Figure 21 define pressure from
   X position, X-plate resistance, Z1, and Z2 in 12-bit mode. */
constexpr uint16_t kPenDownZ1 = 0x500u;
constexpr uint16_t kPenDownZ2 = 0xB00u;
constexpr uint16_t kPenUpZ1 = 0x010u;
constexpr uint16_t kPenUpZ2 = 0xFFFu;

}

bool Tsc2017HostState::ShouldRegister() {
    return emu_.TryGet<TiTsc2017Wiring>() != nullptr;
}

void Tsc2017HostState::SetPen(bool down, uint16_t raw_x, uint16_t raw_y) {
    bool notify = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const bool was_down = state_.down;
        const uint16_t old_x = state_.x;
        const uint16_t old_y = state_.y;

        if (down) {
            state_.x = raw_x;
            state_.y = raw_y;
            state_.down = true;
            state_.z1 = kPenDownZ1;
            state_.z2 = kPenDownZ2;
        } else {
            state_.down = false;
            state_.z1 = kPenUpZ1;
            state_.z2 = kPenUpZ2;
        }

        const bool changed =
            was_down != state_.down || old_x != state_.x || old_y != state_.y;
        if (changed && (down || was_down)) {
            penirq_pending_ = true;
            notify = true;
        }
    }
    if (notify) NotifyIrqChanged();
}

Tsc2017HostState::Sample Tsc2017HostState::Get() {
    std::lock_guard<std::mutex> lk(mutex_);
    return state_;
}

bool Tsc2017HostState::PenIrqPending() {
    std::lock_guard<std::mutex> lk(mutex_);
    return penirq_pending_;
}

void Tsc2017HostState::ClearPenIrqPending() {
    bool notify = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        notify = penirq_pending_;
        penirq_pending_ = false;
    }
    if (notify) NotifyIrqChanged();
}

void Tsc2017HostState::SetIrqChangedCallback(IrqChangedCallback cb, void* ctx) {
    std::lock_guard<std::mutex> lk(mutex_);
    irq_cb_ = cb;
    irq_ctx_ = ctx;
}

void Tsc2017HostState::NotifyIrqChanged() {
    IrqChangedCallback cb = nullptr;
    void* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = irq_cb_;
        ctx = irq_ctx_;
    }
    if (cb) cb(ctx);
}

/* SBAS472 p. 19: driver-activation commands force PENIRQ low; with the pen-interrupt
   function disabled the device "cannot detect when the panel is touched". */
bool Tsc2017HostState::PenIrqLineHigh() {
    std::lock_guard<std::mutex> lk(mutex_);
    switch (penirq_mode_) {
    case PenIrqMode::ForcedLow: return false;
    case PenIrqMode::Disabled: return true;
    case PenIrqMode::Enabled: break;
    }
    return !state_.down;
}

void Tsc2017HostState::SetPenIrqMode(PenIrqMode mode, bool notify) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        changed = penirq_mode_ != mode;
        penirq_mode_ = mode;
    }
    if (changed && notify) NotifyIrqChanged();
}
