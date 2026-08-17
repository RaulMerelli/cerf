#include "ti_tsc2017_host_state.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

REGISTER_SERVICE(Tsc2017HostState);

namespace {

constexpr uint16_t kPenDownZ1 = 0x500u;
constexpr uint16_t kPenDownZ2 = 0xB00u;
/* touch.dll TSCSample pressure gate = (((Z2_hi<<8)/max(Z1_hi,1)) - 256)*X/4096,
   returning pen-up only when pressure >= 520 and Z2_hi > 0xFA (bsptouch.c). A
   tiny non-zero Z1 with full-scale Z2 on release crosses that gate without
   emitting an extra coordinate. */
constexpr uint16_t kPenUpZ1 = 0x010u;
constexpr uint16_t kPenUpZ2 = 0xFFFu;

}  // namespace

bool Tsc2017HostState::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
}

void Tsc2017HostState::SetPen(bool down, uint16_t raw_x, uint16_t raw_y) {
    bool notify = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const bool     was_down = state_.down;
        const bool     was_penirq_low = state_.penirq_low;
        const uint16_t old_x = state_.x;
        const uint16_t old_y = state_.y;

        if (down) {
            state_.x = raw_x;
            state_.y = raw_y;
            state_.down = true;
            state_.penirq_low = true;
            state_.z1 = kPenDownZ1;
            state_.z2 = kPenDownZ2;
        } else {
            state_.down = false;
            state_.penirq_low = false;
            state_.z1 = kPenUpZ1;
            state_.z2 = kPenUpZ2;
        }

        const bool changed = was_down != state_.down || old_x != state_.x ||
                             old_y != state_.y ||
                             was_penirq_low != state_.penirq_low;
        if (changed && (down || was_down || was_penirq_low)) {
            penirq_pending_ = true;
            notify = true;
        }
    }
    if (notify)
        NotifyIrqChanged();
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
    if (notify)
        NotifyIrqChanged();
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
    if (cb)
        cb(ctx);
}

bool Tsc2017HostState::PenIrqLineHigh() {
    std::lock_guard<std::mutex> lk(mutex_);
    return !state_.penirq_low;
}

