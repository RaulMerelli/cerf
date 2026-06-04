#define NOMINMAX

#include "host_canvas_input.h"

#include "../core/cerf_emulator.h"
#include "host_canvas.h"
#include "host_guest_cursor.h"
#include "input_mode_selector.h"
#include "keyboard_input.h"
#include "memory_visualizer.h"
#include "pointer_input.h"
#include "touch_input.h"

REGISTER_SERVICE(HostCanvasInput);

void HostCanvasInput::ReleasePenIfDown() {
    if (!pen_down_) return;
    pen_down_ = false;
    if (GetCapture() == emu_.Get<HostCanvas>().Hwnd()) ReleaseCapture();
    if (auto* t = emu_.TryGet<TouchInput>()) t->OnCaptureLost();
}

bool HostCanvasInput::RoutePointerInput(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* pi = emu_.TryGet<PointerInput>();
    auto& hc = emu_.Get<HostCanvas>();
    if (!pi || hc.CurrentTab() != HostCanvas::Tab::Framebuffer) return false;

    if (msg == WM_MOUSEWHEEL) {
        POINT p = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
        ScreenToClient(hwnd, &p);   /* wheel lp is screen, not client */
        int sx, sy;
        hc.HostToGuest(p.x, p.y, sx, sy);
        hc.ClampGuest(sx, sy);
        pi->OnWheel(sx, sy, (int)(short)HIWORD(wp));
        return true;
    }
    if (msg == WM_CAPTURECHANGED) { pi->OnCaptureLost(); return true; }

    const bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
    const bool up   = msg == WM_LBUTTONUP   || msg == WM_RBUTTONUP   || msg == WM_MBUTTONUP;
    if (!down && !up && msg != WM_MOUSEMOVE) return false;

    if (down) { SetFocus(hwnd); SetCapture(hwnd); }

    int sx, sy;
    hc.HostToGuest((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), sx, sy);
    hc.ClampGuest(sx, sy);

    const WORD ks = LOWORD(wp);
    uint32_t mask = 0;
    if (ks & MK_LBUTTON) mask |= kPointerLeft;
    if (ks & MK_RBUTTON) mask |= kPointerRight;
    if (ks & MK_MBUTTON) mask |= kPointerMiddle;
    pi->OnMove(sx, sy, mask);

    if (up && mask == 0) ReleaseCapture();
    return true;
}

bool HostCanvasInput::Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out) {
    auto& hc = emu_.Get<HostCanvas>();
    out = 0;

    /* Over the framebuffer, make the host cursor BE the guest's cursor shape
       (vmware model). HostGuestCursor returns the guest's HCURSOR, or NULL if
       the guest hid it; not handled => no guest cursor (PocketPC) => host keeps
       its stock arrow. */
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT &&
        hc.CurrentTab() == HostCanvas::Tab::Framebuffer) {
        bool active = false;
        HCURSOR cur = emu_.Get<HostGuestCursor>().Resolve(active);
        if (active) {
            SetCursor(cur);
            out = TRUE;
            return true;
        }
        return false;
    }

    if (hc.CurrentTab() == HostCanvas::Tab::MemoryVisualizer)
        if (auto* mv = emu_.TryGet<MemoryVisualizer>())
            if (mv->HandleInput(hwnd, msg, wp, lp)) return true;

    auto* sel = emu_.TryGet<InputModeSelector>();
    const bool touch_selected = sel && sel->Mode() == InputMode::Touch;
    if (!touch_selected && RoutePointerInput(hwnd, msg, wp, lp)) return true;

    switch (msg) {
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            if (hc.CurrentTab() != HostCanvas::Tab::Framebuffer) return true;
            int sx, sy;
            if (!hc.HostToGuest((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), sx, sy))
                return true;
            pen_down_ = true;
            SetCapture(hwnd);
            if (auto* t = emu_.TryGet<TouchInput>()) t->OnPenDown(sx, sy);
            return true;
        }
        case WM_MOUSEMOVE: {
            if (!pen_down_) return true;
            int sx, sy;
            hc.HostToGuest((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), sx, sy);
            hc.ClampGuest(sx, sy);
            if (auto* t = emu_.TryGet<TouchInput>()) t->OnPenMove(sx, sy);
            return true;
        }
        case WM_LBUTTONUP: {
            if (!pen_down_) return true;
            pen_down_ = false;
            ReleaseCapture();
            int sx, sy;
            hc.HostToGuest((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), sx, sy);
            hc.ClampGuest(sx, sy);
            if (auto* t = emu_.TryGet<TouchInput>()) t->OnPenUp(sx, sy);
            return true;
        }
        case WM_CAPTURECHANGED: {
            if (pen_down_) {
                pen_down_ = false;
                if (auto* t = emu_.TryGet<TouchInput>()) t->OnCaptureLost();
            }
            return true;
        }
        case WM_KEYDOWN:
        case WM_KEYUP:
            if (hc.CurrentTab() == HostCanvas::Tab::Framebuffer)
                if (auto* k = emu_.TryGet<KeyboardInput>())
                    k->OnHostKey(static_cast<uint8_t>(wp), msg == WM_KEYUP);
            return true;
        default:
            return false;
    }
}
