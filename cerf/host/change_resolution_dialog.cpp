#define NOMINMAX
#include "change_resolution_dialog.h"

#include <uxtheme.h>
#include <vssym32.h>

#include "../boot/guest_cold_boot.h"
#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../peripherals/cerf_virt/cerf_virt_framebuffer.h"
#include "../peripherals/cerf_virt/cerf_virt_resize.h"
#include "../socs/guest_cpu_reset.h"
#include "guest_additions_ui_policy.h"
#include "host_dark_mode.h"
#include "host_window.h"

#include <cstdio>

REGISTER_SERVICE(ChangeResolutionDialog);

namespace {

constexpr wchar_t kClass[] = L"CerfChangeResolutionDlg";

constexpr int kClientW = 380;
constexpr int kClientH = 380;

/* Group frames (caption sits on the top edge). */
constexpr RECT kGroupRes   = { 14, 12,  366, 82  };
constexpr RECT kGroupDpi   = { 14, 92,  366, 150 };
constexpr RECT kGroupReset = { 14, 160, 366, 322 };

constexpr uint32_t kMinDim = 64;
constexpr uint32_t kMaxDim = 8192;

enum : int {
    IDC_W_EDIT   = 4001,
    IDC_H_EDIT   = 4002,
    IDC_RB_NONE  = 4003,
    IDC_RB_SOFT  = 4004,
    IDC_RB_HARD  = 4005,
    IDC_DPI_EDIT = 4006,
};

}  /* namespace */

bool ChangeResolutionDialog::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void ChangeResolutionDialog::OnReady() {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &ChangeResolutionDialog::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);   /* ERROR_CLASS_ALREADY_EXISTS is benign */
}

void ChangeResolutionDialog::BuildControls(HWND hwnd) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                  int x, int y, int w, int h, int id) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, hwnd, (HMENU)(INT_PTR)id, inst,
                               nullptr);
    };

    mk(L"STATIC", L"Width:", 0, 28, 42, 50, 18, 0);
    HWND we = mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | WS_GROUP | ES_NUMBER,
                 84, 39, 90, 24, IDC_W_EDIT);
    mk(L"STATIC", L"Height:", 0, 200, 42, 52, 18, 0);
    HWND he = mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_NUMBER,
                 256, 39, 90, 24, IDC_H_EDIT);

    mk(L"STATIC", L"DPI:", 0, 28, 121, 50, 18, 0);
    HWND de = mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_NUMBER,
                 84, 118, 90, 24, IDC_DPI_EDIT);

    mk(L"STATIC",
       L"Windows CE ≤ 3 needs at least a soft reset to use the new "
       L"resolution. A DPI change requires a reset.",
       0, 28, 178, 324, 52, 0);
    mk(L"BUTTON", L"Do not reset",
       BS_OWNERDRAW | WS_GROUP | WS_TABSTOP, 28, 236, 320, 24, IDC_RB_NONE);
    mk(L"BUTTON", L"Soft reset", BS_OWNERDRAW | WS_TABSTOP, 28, 264, 320, 24,
       IDC_RB_SOFT);
    mk(L"BUTTON", L"Hard reset", BS_OWNERDRAW | WS_TABSTOP, 28, 292, 320, 24,
       IDC_RB_HARD);

    mk(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP,
       kClientW - 188, kClientH - 44, 86, 30, IDOK);
    mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP,
       kClientW - 96, kClientH - 44, 86, 30, IDCANCEL);

    HFONT gui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageW(c, WM_SETFONT, (WPARAM)gui, TRUE);

    auto& fb = emu_.Get<CerfVirtFramebuffer>();
    wchar_t buf[16];
    _snwprintf_s(buf, _TRUNCATE, L"%u", fb.Width());
    SetWindowTextW(we, buf);
    _snwprintf_s(buf, _TRUNCATE, L"%u", fb.Height());
    SetWindowTextW(he, buf);

    auto& devcfg = emu_.Get<DeviceConfig>();
    _snwprintf_s(buf, _TRUNCATE, L"%u",
                 devcfg.screen_dpi ? devcfg.screen_dpi : 96u);
    SetWindowTextW(de, buf);

    reset_choice_ = emu_.Get<GuestAdditionsUiPolicy>().DefaultResetIsSoft() ? 1 : 0;
}

void ChangeResolutionDialog::PaintGroups(HDC dc) {
    const bool dark = emu_.Get<HostDarkMode>().IsDark();
    PaintGroup(dc, kGroupRes,   L"Resolution",   dark);
    PaintGroup(dc, kGroupDpi,   L"Display DPI",  dark);
    PaintGroup(dc, kGroupReset, L"Reset device", dark);
}

void ChangeResolutionDialog::PaintGroup(HDC dc, const RECT& f,
                                        const wchar_t* cap, bool dark) {
    HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(80, 80, 80) : RGB(160, 160, 160));
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, f.left, f.top, f.right, f.bottom);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);

    HFONT font = emu_.Get<HostDarkMode>().UiFont();
    HGDIOBJ of = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SIZE ts;
    GetTextExtentPoint32W(dc, cap, lstrlenW(cap), &ts);
    RECT cr = { f.left + 10, f.top - ts.cy / 2,
                f.left + 18 + ts.cx, f.top + ts.cy / 2 };
    HBRUSH bg = dark ? emu_.Get<HostDarkMode>().BgBrush()
                     : GetSysColorBrush(COLOR_BTNFACE);
    FillRect(dc, &cr, bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, dark ? emu_.Get<HostDarkMode>().TextColor()
                          : GetSysColor(COLOR_BTNTEXT));
    RECT tr = { f.left + 14, cr.top, cr.right, cr.bottom };
    DrawTextW(dc, cap, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, of);
}

void ChangeResolutionDialog::DrawRadio(const DRAWITEMSTRUCT* di) {
    HDC  dc = di->hDC;
    RECT rc = di->rcItem;
    const int  idx     = (int)di->CtlID - IDC_RB_NONE;
    const bool checked = (reset_choice_ == idx);
    const bool dark    = emu_.Get<HostDarkMode>().IsDark();

    HBRUSH bg = dark ? emu_.Get<HostDarkMode>().BgBrush()
                     : GetSysColorBrush(COLOR_BTNFACE);
    FillRect(dc, &rc, bg);

    const int gh    = rc.bottom - rc.top;
    const int state = checked ? RBS_CHECKEDNORMAL : RBS_UNCHECKEDNORMAL;
    SIZE gsz = { 13, 13 };
    if (HTHEME th = OpenThemeData(hwnd_, L"Button")) {
        SIZE s;
        if (SUCCEEDED(GetThemePartSize(th, dc, BP_RADIOBUTTON, state, nullptr,
                                       TS_DRAW, &s)))
            gsz = s;
        RECT gr = { rc.left, rc.top + (gh - gsz.cy) / 2,
                    rc.left + gsz.cx, rc.top + (gh - gsz.cy) / 2 + gsz.cy };
        DrawThemeBackground(th, dc, BP_RADIOBUTTON, state, &gr, nullptr);
        CloseThemeData(th);
    } else {
        RECT gr = { rc.left, rc.top + (gh - gsz.cy) / 2,
                    rc.left + gsz.cx, rc.top + (gh - gsz.cy) / 2 + gsz.cy };
        DrawFrameControl(dc, &gr, DFC_BUTTON,
                         DFCS_BUTTONRADIO | (checked ? DFCS_CHECKED : 0));
    }

    wchar_t txt[32];
    GetWindowTextW(di->hwndItem, txt, 32);
    RECT tr = { rc.left + gsz.cx + 8, rc.top, rc.right, rc.bottom };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, dark ? emu_.Get<HostDarkMode>().TextColor()
                          : GetSysColor(COLOR_BTNTEXT));
    HFONT f  = (HFONT)SendMessageW(di->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ ofn = f ? SelectObject(dc, f) : nullptr;
    DrawTextW(dc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (di->itemState & ODS_FOCUS) {
        RECT fr = tr;
        DrawTextW(dc, txt, -1, &fr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_CALCRECT);
        fr.top = rc.top;
        fr.bottom = rc.bottom;
        DrawFocusRect(dc, &fr);
    }
    if (ofn) SelectObject(dc, ofn);
}

bool ChangeResolutionDialog::Apply(HWND hwnd) {
    BOOL okw = FALSE, okh = FALSE;
    const uint32_t w = GetDlgItemInt(hwnd, IDC_W_EDIT, &okw, FALSE);
    const uint32_t h = GetDlgItemInt(hwnd, IDC_H_EDIT, &okh, FALSE);
    if (!okw || !okh || w < kMinDim || h < kMinDim || w > kMaxDim ||
        h > kMaxDim) {
        wchar_t msg[160];
        _snwprintf_s(msg, _TRUNCATE,
                     L"Enter a width and height between %u and %u pixels.",
                     kMinDim, kMaxDim);
        MessageBoxW(hwnd, msg, L"Change resolution", MB_OK | MB_ICONWARNING);
        return false;
    }

    BOOL okd = FALSE;
    const uint32_t dpi = GetDlgItemInt(hwnd, IDC_DPI_EDIT, &okd, FALSE);
    if (!okd || dpi < 1) {
        MessageBoxW(hwnd, L"Enter a DPI value of at least 1.",
                    L"Change resolution", MB_OK | MB_ICONWARNING);
        return false;
    }

    auto& dc = emu_.Get<DeviceConfig>();
    dc.board_configurable_screen_width    = w;
    dc.board_configurable_screen_height   = h;
    dc.board_configurable_screen_explicit = true;
    dc.screen_dpi                         = dpi;

    auto& win = emu_.Get<HostWindow>();
    if (reset_choice_ == 1) {
        win.SetGuestResolution(w, h);   /* stage host fb + canvas for the reboot */
        win.FitToResolution(w, h);
        emu_.Get<GuestCpuReset>().WarmReset();
    } else if (reset_choice_ == 2) {
        win.SetGuestResolution(w, h);
        win.FitToResolution(w, h);
        emu_.Get<GuestColdBoot>().RequestHardReset();
    } else {
        win.FitToResolution(w, h);
        emu_.Get<CerfVirtResize>().RequestResize(w, h, 32u);
    }
    return true;
}

void ChangeResolutionDialog::Show() {
    if (hwnd_) { SetForegroundWindow(hwnd_); return; }

    HWND owner = emu_.Get<HostWindow>().Hwnd();
    done_ = false;

    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_DLGFRAME | WS_POPUP;
    RECT wr = { 0, 0, kClientW, kClientH };
    AdjustWindowRect(&wr, style, FALSE);
    const int ww = wr.right - wr.left;
    const int wh = wr.bottom - wr.top;
    RECT orc = { 0, 0, 0, 0 };
    GetWindowRect(owner, &orc);
    const int x = orc.left + ((orc.right - orc.left) - ww) / 2;
    const int y = orc.top  + ((orc.bottom - orc.top) - wh) / 2;

    EnableWindow(owner, FALSE);
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"Change resolution",
                            style, x, y, ww, wh, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        EnableWindow(owner, TRUE);
        return;
    }

    BuildControls(hwnd_);
    emu_.Get<HostDarkMode>().ApplyToDialog(hwnd_);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);

    MSG msg;
    while (!done_ && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

LRESULT ChangeResolutionDialog::WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                        LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            PaintGroups(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (di->CtlType == ODT_BUTTON && di->CtlID >= IDC_RB_NONE &&
                di->CtlID <= IDC_RB_HARD) {
                DrawRadio(di);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == IDOK) {
                if (Apply(hwnd)) done_ = true;
            } else if (id == IDCANCEL) {
                done_ = true;
            } else if (id >= IDC_RB_NONE && id <= IDC_RB_HARD &&
                       HIWORD(wp) == BN_CLICKED) {
                reset_choice_ = id - IDC_RB_NONE;
                for (int r = IDC_RB_NONE; r <= IDC_RB_HARD; ++r)
                    InvalidateRect(GetDlgItem(hwnd, r), nullptr, FALSE);
            }
            return 0;
        }

        case WM_CLOSE:
            done_ = true;
            return 0;

        case WM_ERASEBKGND:
            if (emu_.Get<HostDarkMode>().EraseBackground((HDC)wp, hwnd))
                return 1;
            break;

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT: {
            LRESULT br;
            if (emu_.Get<HostDarkMode>().HandleCtlColor(msg, wp, br))
                return br;
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ChangeResolutionDialog::WndProcStatic(HWND hwnd, UINT msg,
                                                       WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<ChangeResolutionDialog*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}
