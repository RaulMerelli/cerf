#include "jornada720_led.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_widget_registry.h"
#include "../../socs/sa1110/sa1110_gpio.h"
#include "../board_detector.h"

REGISTER_SERVICE(Jornada720Led);

namespace {
constexpr int kBlinkPeriodMs = 500;
const COLORREF kClrLit  = RGB(120, 255, 140);
const COLORREF kClrDark = RGB(34, 44, 36);
const COLORREF kClrRim  = RGB(150, 160, 150);
}  /* namespace */

bool Jornada720Led::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::Jornada720;
}

void Jornada720Led::OnReady() {
    /* Button line idles high; a press pulls it low (falling edge, GFER13). */
    emu_.Get<Sa1110Gpio>().DriveInputPin(13, /*level=*/true);
    emu_.Get<HostWidgetRegistry>().Register(this);
}

void Jornada720Led::SetState(State s) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    state_ = s;
}

uint8_t Jornada720Led::StatusByte() const {
    switch (CurrentState()) {
        case State::On:    return 0x01;
        case State::Blink: return 0x03;
        default:           return 0x00;
    }
}

Jornada720Led::State Jornada720Led::CurrentState() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return state_;
}

void Jornada720Led::PressButton() {
    auto& gpio = emu_.Get<Sa1110Gpio>();
    gpio.DriveInputPin(13, false);
    gpio.DriveInputPin(13, true);
}

std::wstring Jornada720Led::Tooltip() const {
    switch (CurrentState()) {
        case State::On:    return L"Notification LED — on (click = press button)";
        case State::Blink: return L"Notification LED — blinking (click = press button)";
        default:           return L"Notification LED — off (click = press button)";
    }
}

std::vector<WidgetMenuItem> Jornada720Led::BuildMenu() {
    std::vector<WidgetMenuItem> items;
    WidgetMenuItem press;
    press.label    = L"Press notification button";
    press.on_click = [this] { PressButton(); };
    items.push_back(std::move(press));
    return items;
}

void Jornada720Led::DrawIcon(HDC dc, const RECT& box) const {
    const State s   = CurrentState();
    const bool  lit = s == State::On ||
                      (s == State::Blink &&
                       (GetTickCount() / kBlinkPeriodMs) % 2 == 0);

    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;
    constexpr int kR = 6;

    HPEN    pen  = CreatePen(PS_SOLID, 1, kClrRim);
    HBRUSH  fill = CreateSolidBrush(lit ? kClrLit : kClrDark);
    HGDIOBJ op   = SelectObject(dc, pen);
    HGDIOBJ ob   = SelectObject(dc, fill);
    Ellipse(dc, cx - kR, cy - kR, cx + kR, cy + kR);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(fill);
    DeleteObject(pen);
}

bool Jornada720Led::PollDirty() {
    const State s   = CurrentState();
    const bool  lit = s == State::On ||
                      (s == State::Blink &&
                       (GetTickCount() / kBlinkPeriodMs) % 2 == 0);
    if (s == last_drawn_state_ && lit == last_drawn_lit_) return false;
    last_drawn_state_ = s;
    last_drawn_lit_   = lit;
    return true;
}
