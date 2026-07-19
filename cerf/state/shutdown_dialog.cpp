#define NOMINMAX
#include "shutdown_dialog.h"

#include "../core/cerf_emulator.h"
#include "../core/config_loader.h"
#include "../core/device_config.h"
#include "../core/log.h"
#include "../cpu/emulated_memory.h"
#include "../host/host_dark_mode.h"
#include "../host/host_window.h"

#include <cstdio>

REGISTER_SERVICE(ShutdownDialog);

namespace {
constexpr wchar_t kClass[]   = L"CerfShutdownDlg";
constexpr UINT    kTimerId   = 1;
constexpr int     kSeconds   = 15;
constexpr int     kClientW   = 416;
constexpr int     kClientH   = 212;
constexpr int     kBarX      = 64;
constexpr int     kBarY      = 100;
constexpr int     kBarW      = 320;
constexpr int     kBarH      = 16;
enum : int { IDC_CHK = 3001, IDC_REMEMBER = 3002 };
}  /* namespace */

void ShutdownDialog::OnReady() {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &ShutdownDialog::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(2));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);   /* ERROR_CLASS_ALREADY_EXISTS is benign */
}

void ShutdownDialog::StopTimer(HWND hwnd) {
    if (!timer_on_) return;
    KillTimer(hwnd, kTimerId);
    timer_on_ = false;
    InvalidateRect(hwnd, nullptr, TRUE);   /* repaint without bar / countdown */
}

void ShutdownDialog::Paint(HWND hwnd) {
    auto& dm = emu_.Get<HostDarkMode>();
    const bool dark = dm.IsDark();

    PAINTSTRUCT ps;
    HDC  hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, dark ? dm.BgBrush() : (HBRUSH)(COLOR_BTNFACE + 1));

    if (HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(2)))
        DrawIconEx(hdc, 16, 18, icon, 32, 32, 0, nullptr, DI_NORMAL);

    SetBkMode(hdc, TRANSPARENT);
    HFONT    font = dm.UiFont();
    HGDIOBJ  old  = SelectObject(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    RECT trc = { 60, 18, rc.right - 12, 56 };
    SetTextColor(hdc, dark ? dm.TextColor() : RGB(0, 0, 0));
    const wchar_t* body = (trigger_ == ShutdownTrigger::DeepSleep)
        ? L"Guest device is in deep sleep mode. Exit CERF?"
        : L"Would you like to shut down CERF?";
    DrawTextW(hdc, body, -1, &trc, DT_LEFT | DT_WORDBREAK);

    if (timer_on_) {
        RECT bar = { kBarX, kBarY, kBarX + kBarW, kBarY + kBarH };
        FrameRect(hdc, &bar, (HBRUSH)GetStockObject(GRAY_BRUSH));
        RECT fill = bar;
        InflateRect(&fill, -1, -1);
        fill.right = fill.left + (fill.right - fill.left) * remaining_ / kSeconds;
        HBRUSH red = CreateSolidBrush(RGB(200, 30, 30));
        FillRect(hdc, &fill, red);
        DeleteObject(red);

        wchar_t cd[32];
        swprintf(cd, 32, L"(%ds left)", remaining_);
        RECT crc = { kBarX, kBarY + kBarH + 4, kBarX + kBarW, kBarY + kBarH + 22 };
        SetTextColor(hdc, dark ? RGB(160, 160, 160) : RGB(96, 96, 96));
        DrawTextW(hdc, cd, -1, &crc, DT_CENTER);
    }
    SelectObject(hdc, old);
    EndPaint(hwnd, &ps);
}

LRESULT ShutdownDialog::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            Paint(hwnd);
            return 0;

        case WM_LBUTTONDOWN:
            StopTimer(hwnd);
            return 0;

        case WM_TIMER:
            if (wp == kTimerId) {
                if (--remaining_ <= 0) {
                    save_ = (SendMessageW(chk_save_, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    KillTimer(hwnd, kTimerId);
                    timer_on_ = false;
                    decided_  = true;
                } else {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == IDOK) {
                save_    = (SendMessageW(chk_save_, BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (SendMessageW(chk_remember_, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    emu_.Get<ConfigLoader>().SaveLastSaveStateMode(save_);
                decided_ = true;
            } else if (id == IDCANCEL) {
                cancelled_ = true;
                decided_   = true;
            } else {
                StopTimer(hwnd);   /* checkbox toggle or other control */
            }
            return 0;
        }

        case WM_CLOSE:             /* the dialog's own X == Cancel */
            cancelled_ = true;
            decided_   = true;
            return 0;

        case WM_ERASEBKGND: {
            if (emu_.Get<HostDarkMode>().EraseBackground((HDC)wp, hwnd))
                return 1;
            break;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            LRESULT br;
            if (emu_.Get<HostDarkMode>().HandleCtlColor(msg, wp, br))
                return br;
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ShutdownDialog::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<ShutdownDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShutdownDialog::DismissAsCancel() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

ShutdownChoice ShutdownDialog::Show(ShutdownTrigger trigger) {
    HWND owner = emu_.Get<HostWindow>().Hwnd();

    const bool with_countdown = (trigger == ShutdownTrigger::WindowClose);
    trigger_   = trigger;
    decided_   = false;
    cancelled_ = false;
    save_      = false;
    timer_on_  = with_countdown;
    remaining_ = kSeconds;

    const unsigned long long mb =
        emu_.Get<EmulatedMemory>().VolatileByteCount() >> 20;

    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_DLGFRAME | WS_POPUP;
    RECT wr = { 0, 0, kClientW, kClientH };
    AdjustWindowRect(&wr, style, FALSE);
    const int ww = wr.right - wr.left;
    const int wh = wr.bottom - wr.top;
    RECT orc = { 0, 0, 0, 0 };
    GetWindowRect(owner, &orc);
    const int x = orc.left + ((orc.right - orc.left) - ww) / 2;
    const int y = orc.top + ((orc.bottom - orc.top) - wh) / 2;

    EnableWindow(owner, FALSE);
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"Shut down CERF",
                            style, x, y, ww, wh, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        EnableWindow(owner, TRUE);
        return ShutdownChoice::Exit;
    }

    HINSTANCE inst = GetModuleHandleW(nullptr);

    wchar_t chk_text[64];
    swprintf(chk_text, 64, L"Save the state (%llu MB)", mb);
    chk_save_ = CreateWindowW(L"BUTTON", chk_text,
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              64, 60, kBarW, 22, hwnd_, (HMENU)(INT_PTR)IDC_CHK,
                              inst, nullptr);
    SendMessageW(chk_save_, BM_SETCHECK,
                 emu_.Get<DeviceConfig>().last_save_state_mode
                     ? BST_CHECKED : BST_UNCHECKED, 0);

    chk_remember_ = CreateWindowW(L"BUTTON", L"Remember choice",
                                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                  16, kClientH - 41, 200, 22, hwnd_,
                                  (HMENU)(INT_PTR)IDC_REMEMBER, inst, nullptr);

    CreateWindowW(L"BUTTON", L"OK",
                  WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                  kClientW - 184, kClientH - 44, 84, 28, hwnd_,
                  (HMENU)(INT_PTR)IDOK, inst, nullptr);
    CreateWindowW(L"BUTTON",
                  trigger == ShutdownTrigger::DeepSleep ? L"Resume" : L"Cancel",
                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  kClientW - 92, kClientH - 44, 84, 28, hwnd_,
                  (HMENU)(INT_PTR)IDCANCEL, inst, nullptr);

    /* Dark title bar + dark-theme every child + modern UI font, one call. */
    emu_.Get<HostDarkMode>().ApplyToDialog(hwnd_);

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    if (with_countdown) SetTimer(hwnd_, kTimerId, 1000, nullptr);

    MSG msg;
    while (!decided_ && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (timer_on_) KillTimer(hwnd_, kTimerId);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (cancelled_) return ShutdownChoice::Cancel;
    return save_ ? ShutdownChoice::ExitSave : ShutdownChoice::Exit;
}
