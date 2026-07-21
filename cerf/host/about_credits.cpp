#define NOMINMAX
#include "about_credits.h"

#include <commctrl.h>
#include <wchar.h>

#include <algorithm>

#include "../core/cerf_emulator.h"
#include "host_dark_mode.h"
#include "host_link_opener.h"

REGISTER_SERVICE(AboutCredits);

namespace {

constexpr wchar_t kClass[] = L"CerfAboutCredits";

struct Contributor {
    const wchar_t* name;
    const wchar_t* url;
    const wchar_t* contribution;
    bool           anonymous;
};

constexpr Contributor kContributors[] = {
    { L"Raul Merelli", nullptr, L"Siemens devices emulation", false },
    { L"Karpour",      nullptr, L"Jornada 820 and EM500 ROMs, project support",
      false },
    { L"Anonymous 1",  nullptr, L"NEC MP700, Toricomail, Sharp HC-4100, Velo 1 "
                                L"and Nino 300 ROMs, project support", true },
};

constexpr wchar_t kHeader[] = L"Thanks to project contributors:";

constexpr wchar_t kFooter[] =
    L"Thanks to everyone else affiliated with or supporting the project who "
    L"did not make it into this list!";

constexpr int kScrollbarGutterDip = 20;
constexpr int kEntryGapDip        = 2;
constexpr int kBlockGapDip        = 10;
constexpr int kFallbackLineDip    = 18;
constexpr int kScrollStepDip      = 16;

std::wstring ContributorMarkup(const Contributor& c) {
    std::wstring s;
    if (c.url) {
        s += L"<a href=\"";
        s += c.url;
        s += L"\">";
        s += c.name;
        s += L"</a>";
    } else {
        s += c.name;
    }
    s += L"  -  ";
    s += c.contribution;
    return s;
}

}

int AboutCredits::S(int v) const {
    return MulDiv(v, (int)dpi_, USER_DEFAULT_SCREEN_DPI);
}

void AboutCredits::OnReady() {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &AboutCredits::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
}

HWND AboutCredits::Create(HWND parent, HFONT font, int x, int y, int w, int h,
                          UINT dpi) {
    dpi_        = dpi;
    font_       = font;
    scroll_pos_ = 0;
    content_h_  = 0;
    lines_.clear();

    pane_ = CreateWindowExW(0, kClass, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                            x, y, w, h, parent, nullptr,
                            GetModuleHandleW(nullptr), this);

    BuildLines(w - S(kScrollbarGutterDip));
    UpdateScrollInfo();
    return pane_;
}

void AboutCredits::BuildLines(int text_w) {
    int y = 0;

    AddLine(kHeader, text_w, y);
    y += S(kBlockGapDip);

    std::vector<const Contributor*> sorted;
    sorted.reserve(std::size(kContributors));
    for (const Contributor& c : kContributors) sorted.push_back(&c);
    std::sort(sorted.begin(), sorted.end(),
              [](const Contributor* a, const Contributor* b) {
                  if (a->anonymous != b->anonymous) return !a->anonymous;
                  return _wcsicmp(a->name, b->name) < 0;
              });

    for (const Contributor* c : sorted) {
        AddLine(ContributorMarkup(*c), text_w, y);
        y += S(kEntryGapDip);
    }

    y += S(kBlockGapDip);
    AddLine(kFooter, text_w, y);

    content_h_ = y;
}

void AboutCredits::AddLine(const std::wstring& markup, int text_w, int& y) {
    const bool has_link = markup.find(L"<a ") != std::wstring::npos;
    const DWORD style = WS_CHILD | WS_VISIBLE | LWS_TRANSPARENT |
                        (has_link ? WS_TABSTOP : 0u);

    HWND h = CreateWindowExW(0, L"SysLink", markup.c_str(), style,
                             0, y, text_w, S(kFallbackLineDip), pane_,
                             nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)font_, FALSE);

    SIZE ideal = { 0, 0 };
    SendMessageW(h, LM_GETIDEALSIZE, (WPARAM)text_w, (LPARAM)&ideal);
    const int line_h = ideal.cy > 0 ? ideal.cy : S(kFallbackLineDip);
    SetWindowPos(h, nullptr, 0, y, text_w, line_h,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    lines_.push_back({ h, y, has_link });
    y += line_h;
}

bool AboutCredits::LineHasLink(HWND h) const {
    for (const Line& l : lines_)
        if (l.hwnd == h) return l.has_link;
    return false;
}

void AboutCredits::UpdateScrollInfo() {
    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(pane_, &rc);

    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin  = 0;
    si.nMax  = content_h_ > 0 ? content_h_ - 1 : 0;
    si.nPage = (UINT)(rc.bottom - rc.top);
    si.nPos  = scroll_pos_;
    SetScrollInfo(pane_, SB_VERT, &si, TRUE);
}

void AboutCredits::ScrollTo(int pos) {
    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(pane_, &rc);

    const int max_pos = std::max(0, content_h_ - (int)(rc.bottom - rc.top));
    pos = std::clamp(pos, 0, max_pos);
    if (pos == scroll_pos_) return;
    scroll_pos_ = pos;

    HDWP dwp = BeginDeferWindowPos((int)lines_.size());
    for (const Line& l : lines_)
        dwp = DeferWindowPos(dwp, l.hwnd, nullptr, 0, l.y - scroll_pos_, 0, 0,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
    EndDeferWindowPos(dwp);

    SetScrollPos(pane_, SB_VERT, scroll_pos_, TRUE);
    InvalidateRect(pane_, nullptr, TRUE);
}

LRESULT AboutCredits::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_VSCROLL: {
            RECT rc = { 0, 0, 0, 0 };
            GetClientRect(hwnd, &rc);
            const int page = rc.bottom - rc.top;
            int pos = scroll_pos_;
            switch (LOWORD(wp)) {
                case SB_LINEUP:   pos -= S(kScrollStepDip); break;
                case SB_LINEDOWN: pos += S(kScrollStepDip); break;
                case SB_PAGEUP:   pos -= page;              break;
                case SB_PAGEDOWN: pos += page;              break;
                case SB_TOP:      pos = 0;                  break;
                case SB_BOTTOM:   pos = content_h_;         break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: {
                    SCROLLINFO si = { sizeof(si) };
                    si.fMask = SIF_TRACKPOS;
                    GetScrollInfo(hwnd, SB_VERT, &si);
                    pos = si.nTrackPos;
                    break;
                }
            }
            ScrollTo(pos);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            ScrollTo(scroll_pos_ - delta * S(kScrollStepDip) * 3 / WHEEL_DELTA);
            return 0;
        }

        case WM_NOTIFY: {
            auto* nh = reinterpret_cast<NMHDR*>(lp);
            if (nh->code == NM_CLICK || nh->code == NM_RETURN) {
                emu_.Get<HostLinkOpener>().OpenNotified(hwnd, lp);
                return 0;
            }
            if (nh->code == NM_CUSTOMDRAW && LineHasLink(nh->hwndFrom)) {
                LRESULT out = 0;
                if (emu_.Get<HostDarkMode>().HandleLinkCustomDraw(lp, out))
                    return out;
            }
            break;
        }

        case WM_ERASEBKGND:
            if (emu_.Get<HostDarkMode>().EraseBackground((HDC)wp, hwnd))
                return 1;
            break;

        case WM_CTLCOLORSTATIC: {
            LRESULT br = 0;
            if (emu_.Get<HostDarkMode>().HandleCtlColor(msg, wp, br))
                return br;
            break;
        }

        case WM_NCDESTROY:
            pane_ = nullptr;
            lines_.clear();
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK AboutCredits::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp,
                                             LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<AboutCredits*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}
