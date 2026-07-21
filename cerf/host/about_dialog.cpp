#define NOMINMAX
#include "about_dialog.h"

#include <commctrl.h>
#include <gdiplus.h>

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../core/string_utils.h"
#include "../version.h"
#include "about_credits.h"
#include "host_dark_mode.h"
#include "host_dpi.h"
#include "host_gdiplus.h"
#include "host_link_opener.h"
#include "host_window.h"

REGISTER_SERVICE(AboutDialog);

AboutDialog::~AboutDialog() {
    if (title_font_) DeleteObject(title_font_);
    if (ui_font_)    DeleteObject(ui_font_);
}

namespace {

constexpr wchar_t kClass[] = L"CerfAboutDlg";

/* cerf/assets/icons_sources/about_band.svg */
constexpr int kBandDipW = 400;
constexpr int kBandDipH = 112;

constexpr int kTitleDy   = 16;
constexpr int kSubDy     = 46;
constexpr int kTagDy     = 68;
constexpr int kDevDy     = 94;
constexpr int kMadeByDy  = 120;
constexpr int kCreditsDy = 150;
constexpr int kCreditsH  = 96;
constexpr int kLinksDy   = 262;
constexpr int kContentH  = 338;
constexpr int kCloseGap  = 42;
constexpr int kNoDeviceDrop = kMadeByDy - kDevDy;

enum : int {
    IDC_TITLE   = 5001,
    IDC_SUBTITLE,
    IDC_TAGLINE,
    IDC_DEVICE,
    IDC_MADEBY_PREFIX,
    IDC_MADEBY,
    IDC_LINKS,
};

const wchar_t* BandResourceForDpi(UINT dpi) {
    const int pct = MulDiv(100, (int)dpi, USER_DEFAULT_SCREEN_DPI);
    if (pct <= 100) return L"ABOUT_BAND_100";
    if (pct <= 125) return L"ABOUT_BAND_125";
    if (pct <= 150) return L"ABOUT_BAND_150";
    if (pct <= 200) return L"ABOUT_BAND_200";
    return L"ABOUT_BAND_300";
}

}  /* namespace */

int AboutDialog::S(int v) const {
    return MulDiv(v, (int)dpi_, USER_DEFAULT_SCREEN_DPI);
}

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

BOOL CALLBACK AboutDialog::SetChildFontProc(HWND child, LPARAM font) {
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

void AboutDialog::BuildControls(HWND hwnd, bool with_device) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                  int x, int y, int w, int h, int id) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, hwnd, (HMENU)(INT_PTR)id, inst,
                               nullptr);
    };

    const int cb = band_px_h_;
    const int clientH = cb + S(kContentH - layout_drop_);
    const int tx = S(20), tw = band_px_w_ - S(40);

    title_ = mk(L"STATIC", L"CE Runtime Foundation", SS_LEFT,
                tx, cb + S(kTitleDy), tw, S(28), IDC_TITLE);

    mk(L"STATIC", L"Version " CERF_VERSION_DISPLAY_WSTR, SS_LEFT,
       tx, cb + S(kSubDy), tw, S(18), IDC_SUBTITLE);

    mk(L"STATIC", L"A universal Windows CE emulator", SS_LEFT,
       tx, cb + S(kTagDy), tw, S(18), IDC_TAGLINE);

    if (with_device) {
        auto& bd = emu_.Get<BoardContext>();
        std::wstring dev = L"Emulating:  " + Utf8ToWide(BoardContext::BoardName(bd.GetBoard()));
        const char* soc = BoardContext::SocFamilyName(bd.GetSoc());
        if (soc && *soc && bd.GetSoc() != SocFamily::Unknown)
            dev += L"  ·  " + Utf8ToWide(soc);
        mk(L"STATIC", dev.c_str(), SS_LEFT, tx, cb + S(kDevDy), tw, S(18),
           IDC_DEVICE);
    }

    constexpr wchar_t kMadeBy[] = L"Made by ";
    const int made_by_y = cb + S(kMadeByDy - layout_drop_);
    SIZE prefix = { 0, 0 };
    {
        HDC     dc  = GetDC(hwnd);
        HGDIOBJ old = SelectObject(dc, ui_font_);
        GetTextExtentPoint32W(dc, kMadeBy, ARRAYSIZE(kMadeBy) - 1, &prefix);
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
    }

    mk(L"STATIC", kMadeBy, SS_LEFT, tx, made_by_y, prefix.cx, S(22),
       IDC_MADEBY_PREFIX);

    mk(L"SysLink",
       L"<a href=\"https://yaroslavkibysh.com\">Yaroslav Kibysh</a>",
       LWS_TRANSPARENT, tx + prefix.cx, made_by_y, tw - prefix.cx, S(22),
       IDC_MADEBY);

    emu_.Get<AboutCredits>().Create(hwnd, ui_font_, tx,
                                    cb + S(kCreditsDy - layout_drop_),
                                    tw, S(kCreditsH), dpi_);

    const int links_y = cb + S(kLinksDy - layout_drop_);
    HWND links = mk(
        L"SysLink",
        L"<a href=\"https://cerf.cx\">Website</a>"
        L"      ·      "
        L"<a href=\"https://discord.gg/QREE9Y2v2d\">Discord</a>",
        LWS_TRANSPARENT, tx, links_y, tw, S(22), IDC_LINKS);
    if (links) {
        SIZE ideal = { 0, 0 };
        if (SendMessageW(links, LM_GETIDEALSIZE, (WPARAM)tw, (LPARAM)&ideal) &&
            ideal.cx > 0)
            SetWindowPos(links, nullptr, tx, links_y,
                         ideal.cx, ideal.cy > 0 ? ideal.cy : S(22),
                         SWP_NOZORDER | SWP_NOACTIVATE);
    }

    mk(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP,
       band_px_w_ - tx - S(100), clientH - S(kCloseGap), S(100), S(30), IDOK);
}

void AboutDialog::CreateFonts() {
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (emu_.Get<HostDpi>().NonClientMetricsForDpi(ncm, dpi_))
        ui_font_ = CreateFontIndirectW(&ncm.lfMessageFont);

    title_font_ = CreateFontW(-S(19), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
}

void AboutDialog::ApplyCustomFonts() {
    if (ui_font_)
        EnumChildWindows(hwnd_, &AboutDialog::SetChildFontProc, (LPARAM)ui_font_);
    if (title_) SendMessageW(title_, WM_SETFONT, (WPARAM)title_font_, TRUE);
}

void AboutDialog::PaintBand(HDC dc, int origin_x, int origin_y) {
    if (!band_) return;
    const UINT bw = band_->GetWidth(), bh = band_->GetHeight();
    if (bw == 0 || bh == 0) return;

    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::Rect dst(-origin_x, -origin_y, (int)bw, (int)bh);
    g.DrawImage(band_, dst, 0, 0, (int)bw, (int)bh, Gdiplus::UnitPixel);
}

void AboutDialog::Show() {
    Run(emu_.Get<HostWindow>().Hwnd(), true);
}

void AboutDialog::ShowStandalone() {
    Run(nullptr, false);
}

void AboutDialog::Run(HWND owner, bool with_device) {
    if (hwnd_) { SetForegroundWindow(hwnd_); return; }

    done_ = false;
    dpi_  = emu_.Get<HostDpi>().ForWindow(owner);

    const bool show_device =
        with_device && emu_.Get<BoardContext>().GetBoard() != Board::Unknown;
    layout_drop_ = show_device ? 0 : kNoDeviceDrop;

    band_ = emu_.Get<HostGdiPlus>().DecodeResourcePng(BandResourceForDpi(dpi_));
    band_px_w_ = band_ ? (int)band_->GetWidth()  : 0;
    band_px_h_ = band_ ? (int)band_->GetHeight() : 0;
    if (band_px_w_ <= 0 || band_px_h_ <= 0) {
        band_px_w_ = S(kBandDipW);
        band_px_h_ = S(kBandDipH);
    }
    const int clientH = band_px_h_ + S(kContentH - layout_drop_);

    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_DLGFRAME | WS_POPUP;
    const DWORD ex    = WS_EX_DLGMODALFRAME;
    RECT wr = { 0, 0, band_px_w_, clientH };
    emu_.Get<HostDpi>().AdjustForDpi(wr, style, FALSE, ex, dpi_);
    const int ww = wr.right - wr.left;
    const int wh = wr.bottom - wr.top;
    RECT orc = { 0, 0, 0, 0 };
    if (owner) GetWindowRect(owner, &orc);
    else       SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    const int x = orc.left + ((orc.right - orc.left) - ww) / 2;
    const int y = orc.top  + ((orc.bottom - orc.top) - wh) / 2;

    EnableWindow(owner, FALSE);
    hwnd_ = CreateWindowExW(ex, kClass, L"About CE Runtime Foundation", style,
                            x, y, ww, wh, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        EnableWindow(owner, TRUE);
        delete band_; band_ = nullptr;
        return;
    }

    CreateFonts();
    BuildControls(hwnd_, show_device);
    emu_.Get<HostDarkMode>().ApplyToDialog(hwnd_);
    ApplyCustomFonts();
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
    delete band_; band_ = nullptr;
    if (title_font_) { DeleteObject(title_font_); title_font_ = nullptr; }
    if (ui_font_)    { DeleteObject(ui_font_);    ui_font_ = nullptr; }
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
                HDC     mem = CreateCompatibleDC(dc);
                HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
                HGDIOBJ ob  = SelectObject(mem, bmp);
                auto& dm = emu_.Get<HostDarkMode>();
                HBRUSH bg = dm.IsDark() ? dm.BgBrush()
                                        : GetSysColorBrush(COLOR_BTNFACE);
                RECT fr = { 0, 0, w, h };
                FillRect(mem, &fr, bg);
                PaintBand(mem, rc.left, rc.top);
                BitBlt(dc, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);
                SelectObject(mem, ob);
                DeleteObject(bmp);
                DeleteDC(mem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NOTIFY: {
            auto* nh = reinterpret_cast<NMHDR*>(lp);
            if (nh->idFrom == IDC_LINKS || nh->idFrom == IDC_MADEBY) {
                if (nh->code == NM_CLICK || nh->code == NM_RETURN) {
                    emu_.Get<HostLinkOpener>().OpenNotified(hwnd_, lp);
                    return 0;
                }
                if (nh->code == NM_CUSTOMDRAW) {
                    LRESULT out = 0;
                    if (emu_.Get<HostDarkMode>().HandleLinkCustomDraw(lp, out))
                        return out;
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
