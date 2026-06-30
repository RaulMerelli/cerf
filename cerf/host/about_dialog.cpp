#define NOMINMAX
#include "about_dialog.h"

#include <cmath>
#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../core/string_utils.h"
#include "../version.h"
#include "host_dark_mode.h"
#include "host_gdiplus.h"
#include "host_window.h"

REGISTER_SERVICE(AboutDialog);

AboutDialog::~AboutDialog() {
    if (title_font_) DeleteObject(title_font_);
}

namespace {

constexpr wchar_t kClass[] = L"CerfAboutDlg";

constexpr int kClientW = 470;
constexpr int kClientH = 326;

/* Logo box (painted, centred on the top strip). Sized so the Konami spin's
   45° corner sweep clears the title text below it. */
constexpr int kLogoTop = 16;
constexpr int kLogoMax = 80;
constexpr float kEggMaxScale = 1.12f;

enum : int {
    IDC_TITLE   = 5001,
    IDC_SUBTITLE,
    IDC_TAGLINE,
    IDC_DEVICE,
    IDC_LINKS,
};

constexpr int kEggTimer = 1;
constexpr uint64_t kEggDurMs = 1400;

/* ↑ ↑ ↓ ↓ ← → ← → B A */
const int kKonami[] = { VK_UP, VK_UP, VK_DOWN, VK_DOWN, VK_LEFT, VK_RIGHT,
                        VK_LEFT, VK_RIGHT, 'B', 'A' };
constexpr int kKonamiLen = (int)(sizeof(kKonami) / sizeof(kKonami[0]));

/* Hyperlink colour with enough contrast on the dark dialog background; the
   system COLOR_HOTLIGHT blue is too dim against ~RGB(32,32,32). */
constexpr COLORREF kDarkLink = RGB(96, 170, 255);

}  /* namespace */

void AboutDialog::OnReady() {
    /* SysLink lives in ICC_LINK_CLASS, which no other init covers. */
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LINK_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &AboutDialog::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);   /* ERROR_CLASS_ALREADY_EXISTS is benign */
}

void AboutDialog::BuildControls(HWND hwnd) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                  int x, int y, int w, int h, int id) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, hwnd, (HMENU)(INT_PTR)id, inst,
                               nullptr);
    };

    const int tx = 20, tw = kClientW - 40;

    title_ = mk(L"STATIC", L"CE Runtime Foundation", SS_CENTER,
                tx, 128, tw, 28, IDC_TITLE);

    mk(L"STATIC", L"Version " CERF_VERSION_DISPLAY_WSTR, SS_CENTER,
       tx, 158, tw, 18, IDC_SUBTITLE);

    mk(L"STATIC", L"A universal Windows CE emulator", SS_CENTER,
       tx, 180, tw, 18, IDC_TAGLINE);

    /* Current device, straight from the ROM-fingerprinting detector. */
    auto& bd = emu_.Get<BoardContext>();
    std::wstring dev = L"Emulating:  " + Utf8ToWide(bd.BoardName());
    const char* soc = BoardContext::SocFamilyName(bd.GetSoc());
    if (soc && *soc && bd.GetSoc() != SocFamily::Unknown)
        dev += L"  ·  " + Utf8ToWide(soc);
    mk(L"STATIC", dev.c_str(), SS_CENTER, tx, 206, tw, 18, IDC_DEVICE);

    HWND links = mk(
        L"SysLink",
        L"<a href=\"https://github.com/gweslab/cerf\">GitHub</a>"
        L"      ·      "
        L"<a href=\"https://discord.gg/QREE9Y2v2d\">Discord</a>",
        LWS_TRANSPARENT, tx, 238, tw, 22, IDC_LINKS);
    /* Centre the SysLink on its measured ideal width. */
    if (links) {
        SIZE ideal = { 0, 0 };
        if (SendMessageW(links, LM_GETIDEALSIZE, (WPARAM)tw, (LPARAM)&ideal) &&
            ideal.cx > 0)
            SetWindowPos(links, nullptr, (kClientW - ideal.cx) / 2, 238,
                         ideal.cx, ideal.cy > 0 ? ideal.cy : 22,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    }

    mk(L"BUTTON", L"Close", BS_DEFPUSHBUTTON | WS_TABSTOP,
       (kClientW - 100) / 2, kClientH - 42, 100, 30, IDOK);
}

void AboutDialog::ApplyCustomFonts() {
    /* HostDarkMode::ApplyToDialog stamps the UI font on every child; re-stamp the
       title in a larger semibold face afterwards. */
    if (!title_font_)
        title_font_ = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    if (title_) SendMessageW(title_, WM_SETFONT, (WPARAM)title_font_, TRUE);
}

void AboutDialog::PaintLogo(HDC dc, int origin_x, int origin_y) {
    if (!logo_) return;
    const UINT bw = logo_->GetWidth(), bh = logo_->GetHeight();
    if (bw == 0 || bh == 0) return;

    /* Fit the logo into the kLogoMax box, keeping aspect. Centre is expressed
       relative to the target DC's origin so a buffered DC can be offset. */
    float s = (float)kLogoMax / (float)(bw > bh ? bw : bh);
    const int dw = (int)(bw * s), dh = (int)(bh * s);
    const int cx = kClientW / 2 - origin_x;
    const int cy = kLogoTop + kLogoMax / 2 - origin_y;

    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    g.TranslateTransform((Gdiplus::REAL)cx, (Gdiplus::REAL)cy);
    if (egg_active_) {
        g.RotateTransform(egg_angle_);
        g.ScaleTransform(egg_scale_, egg_scale_);
    }
    Gdiplus::Rect dst(-dw / 2, -dh / 2, dw, dh);
    g.DrawImage(logo_, dst, 0, 0, (int)bw, (int)bh, Gdiplus::UnitPixel);
}

bool AboutDialog::OpenLink(LPARAM lp) {
    auto* link = reinterpret_cast<NMLINK*>(lp);
    const wchar_t* url = link->item.szUrl;
    if (url[0]) {
        ShellExecuteW(hwnd_, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    return false;
}

void AboutDialog::TrackKonami(int vk) {
    if (vk == kKonami[konami_idx_]) {
        if (++konami_idx_ >= kKonamiLen) { konami_idx_ = 0; StartEgg(); }
    } else {
        konami_idx_ = (vk == kKonami[0]) ? 1 : 0;
    }
}

void AboutDialog::StartEgg() {
    if (egg_active_ || !hwnd_) return;
    egg_active_ = true;
    egg_start_  = GetTickCount64();
    SetTimer(hwnd_, kEggTimer, 16, nullptr);
}

void AboutDialog::AdvanceEgg() {
    const uint64_t elapsed = GetTickCount64() - egg_start_;
    if (elapsed >= kEggDurMs) {
        egg_active_ = false;
        egg_angle_  = 0.0f;
        egg_scale_  = 1.0f;
        KillTimer(hwnd_, kEggTimer);
    } else {
        const float t  = (float)elapsed / (float)kEggDurMs;        /* 0..1 */
        const float eo = 1.0f - (1.0f - t) * (1.0f - t);           /* ease-out */
        egg_angle_ = 360.0f * eo;
        egg_scale_ = 1.0f + 0.12f * sinf(3.14159265f * t);         /* gentle pulse */
    }
    /* The spinning square sweeps a circle of radius = half-diagonal × max scale;
       invalidate that whole disc so rotated corners leave no smear. */
    const int r = (int)(kLogoMax * 0.70710678f * kEggMaxScale) + 4;
    const int cx = kClientW / 2, cy = kLogoTop + kLogoMax / 2;
    RECT lr = { cx - r, cy - r, cx + r, cy + r };
    InvalidateRect(hwnd_, &lr, FALSE);   /* buffered paint clears its own bg */
}

void AboutDialog::Show() {
    if (hwnd_) { SetForegroundWindow(hwnd_); return; }

    HWND owner = emu_.Get<HostWindow>().Hwnd();
    done_        = false;
    konami_idx_  = 0;
    egg_active_  = false;
    egg_angle_   = 0.0f;
    egg_scale_   = 1.0f;
    logo_        = emu_.Get<HostGdiPlus>().DecodeResourcePng(L"CERF_LOGO");

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
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"About CERF", style,
                            x, y, ww, wh, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        EnableWindow(owner, TRUE);
        delete logo_; logo_ = nullptr;
        return;
    }

    BuildControls(hwnd_);
    emu_.Get<HostDarkMode>().ApplyToDialog(hwnd_);
    ApplyCustomFonts();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);

    MSG msg;
    while (!done_ && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN &&
            (msg.hwnd == hwnd_ || IsChild(hwnd_, msg.hwnd)))
            TrackKonami((int)msg.wParam);
        if (!IsDialogMessageW(hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (egg_active_) KillTimer(hwnd_, kEggTimer);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    delete logo_; logo_ = nullptr;
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

LRESULT AboutDialog::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            const RECT& rc = ps.rcPaint;
            const int w = rc.right - rc.left, h = rc.bottom - rc.top;
            if (w > 0 && h > 0) {
                /* Double-buffer: fill background + draw the (possibly rotating)
                   logo into a memory DC, then blit once - no erase-then-draw
                   flash during the Konami spin. */
                HDC     mem = CreateCompatibleDC(dc);
                HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
                HGDIOBJ ob  = SelectObject(mem, bmp);
                auto& dm = emu_.Get<HostDarkMode>();
                HBRUSH bg = dm.IsDark() ? dm.BgBrush()
                                        : GetSysColorBrush(COLOR_BTNFACE);
                RECT fr = { 0, 0, w, h };
                FillRect(mem, &fr, bg);
                PaintLogo(mem, rc.left, rc.top);
                BitBlt(dc, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);
                SelectObject(mem, ob);
                DeleteObject(bmp);
                DeleteDC(mem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER:
            if (wp == kEggTimer) { AdvanceEgg(); return 0; }
            break;

        case WM_NOTIFY: {
            auto* nh = reinterpret_cast<NMHDR*>(lp);
            if (nh->idFrom == IDC_LINKS) {
                if (nh->code == NM_CLICK || nh->code == NM_RETURN) {
                    OpenLink(lp);
                    return 0;
                }
                if (nh->code == NM_CUSTOMDRAW &&
                    emu_.Get<HostDarkMode>().IsDark()) {
                    auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(lp);
                    if (cd->dwDrawStage == CDDS_PREPAINT)
                        return CDRF_NOTIFYITEMDRAW;
                    if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                        SetTextColor(cd->hdc, kDarkLink);
                        return CDRF_NEWFONT;
                    }
                }
            }
            break;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == IDOK || id == IDCANCEL) done_ = true;
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
        case WM_CTLCOLORBTN: {
            LRESULT br;
            if (emu_.Get<HostDarkMode>().HandleCtlColor(msg, wp, br))
                return br;
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK AboutDialog::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp,
                                            LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<AboutDialog*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}
