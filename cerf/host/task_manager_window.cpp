#define NOMINMAX
#include "task_manager_window.h"

#include <windowsx.h>
#include <commctrl.h>

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../peripherals/cerf_virt/cerf_virt_task_manager.h"
#include "../peripherals/cerf_virt/cerf_virt_task_manager_regs.h"
#include "host_dark_mode.h"
#include "host_window.h"

#include <cstdio>

REGISTER_SERVICE(TaskManagerWindow);

namespace {

constexpr wchar_t kWndClass[] = L"CerfTaskManagerWnd";

constexpr UINT kRefreshTimerId = 1;
constexpr UINT kRefreshMs      = 500;

constexpr int kMargin     = 8;
constexpr int kBottomBarH = 76;
constexpr int kRowH       = 26;
constexpr int kBtnW       = 90;

enum : int {
    IDC_LIST    = 2001,
    IDC_RUNEDIT = 2002,
    IDC_RUN     = 2003,
    IDC_SWITCH  = 2004,
    IDC_KILL    = 2005,
    IDM_ROW_SWITCH = 2101,
    IDM_ROW_KILL   = 2102,
};

constexpr COLORREF kClrOk   = RGB(0, 140, 0);
constexpr COLORREF kClrFail = RGB(190, 0, 0);
constexpr COLORREF kClrInfo = RGB(96, 96, 96);

const wchar_t* ActionName(uint32_t code) {
    switch (code) {
        case CerfVirt::kTmCmdKill:     return L"kill";
        case CerfVirt::kTmCmdSwitchTo: return L"switch to";
        case CerfVirt::kTmCmdRun:      return L"run";
        case CerfVirt::kTmCmdList:     return L"process list";
        default:                       return L"command";
    }
}

bool SameProcs(const std::vector<CerfVirtTaskManager::ProcEntry>& a,
               const std::vector<CerfVirtTaskManager::ProcEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.pid != y.pid || x.parent_pid != y.parent_pid ||
            x.thread_count != y.thread_count ||
            x.base_priority != y.base_priority ||
            x.mem_base != y.mem_base || x.name != y.name)
            return false;
    }
    return true;
}

}  /* namespace */

bool TaskManagerWindow::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void TaskManagerWindow::OnReady() {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &TaskManagerWindow::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);   /* ERROR_CLASS_ALREADY_EXISTS is benign */
}

void TaskManagerWindow::Show() {
    if (hwnd_) {
        if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
        return;
    }

    HWND owner = emu_.Get<HostWindow>().Hwnd();
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT wr = { 0, 0, 560, 420 };
    AdjustWindowRectEx(&wr, style, FALSE, 0);
    hwnd_ = CreateWindowExW(0, kWndClass, L"Guest Task Manager", style,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            wr.right - wr.left, wr.bottom - wr.top,
                            owner, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return;

    BuildControls();
    LayoutControls();

    auto& dm = emu_.Get<HostDarkMode>();
    dm.ApplyToDialog(hwnd_);
    if (dm.IsDark()) {
        ListView_SetBkColor(list_, dm.BgColor());
        ListView_SetTextBkColor(list_, dm.BgColor());
        ListView_SetTextColor(list_, dm.TextColor());
    }

    ShowWindow(hwnd_, SW_SHOW);
    SetTimer(hwnd_, kRefreshTimerId, kRefreshMs, nullptr);

    shown_gen_ = 0;   /* force a repopulate from the next snapshot */
    emu_.Get<CerfVirtTaskManager>().RequestProcessList();
    SetStatus(L"Waiting for guest…", kClrInfo);
}

void TaskManagerWindow::BuildControls() {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    HFONT gui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                  int id) {
        HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                 0, 0, 0, 0, hwnd_, (HMENU)(INT_PTR)id,
                                 inst, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)gui, TRUE);
        return h;
    };

    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                            LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                            0, 0, 0, 0, hwnd_, (HMENU)(INT_PTR)IDC_LIST,
                            inst, nullptr);
    SendMessageW(list_, WM_SETFONT, (WPARAM)gui, TRUE);
    ListView_SetExtendedListViewStyle(list_,
                                      LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP);

    struct { const wchar_t* title; int width; } cols[] = {
        { L"Process",  150 }, { L"PID",      90 }, { L"Parent",   90 },
        { L"Threads",   60 }, { L"Priority", 60 }, { L"Mem base", 90 },
    };
    for (int i = 0; i < (int)(sizeof(cols) / sizeof(cols[0])); ++i) {
        LVCOLUMNW c = {};
        c.mask     = LVCF_TEXT | LVCF_WIDTH;
        c.pszText  = (LPWSTR)cols[i].title;
        c.cx       = cols[i].width;
        ListView_InsertColumn(list_, i, &c);
    }

    btn_switch_ = mk(L"BUTTON", L"Switch to", BS_PUSHBUTTON | WS_TABSTOP, IDC_SWITCH);
    btn_kill_   = mk(L"BUTTON", L"Kill",      BS_PUSHBUTTON | WS_TABSTOP, IDC_KILL);
    run_label_  = mk(L"STATIC", L"Run:", 0, 0);
    run_edit_   = mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                     IDC_RUNEDIT);
    btn_run_    = mk(L"BUTTON", L"Run", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_RUN);
    status_     = mk(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0);

    SendMessageW(run_edit_, EM_SETLIMITTEXT, CerfVirt::kTmRunMaxWchars, 0);
    SetWindowLongPtrW(run_edit_, GWLP_USERDATA, (LONG_PTR)this);
    run_edit_base_proc_ = (WNDPROC)SetWindowLongPtrW(
        run_edit_, GWLP_WNDPROC, (LONG_PTR)&TaskManagerWindow::RunEditProcStatic);
}

void TaskManagerWindow::LayoutControls() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int w = rc.right, h = rc.bottom;

    MoveWindow(list_, kMargin, kMargin, w - 2 * kMargin,
               h - kBottomBarH - 2 * kMargin, TRUE);

    const int row1 = h - kBottomBarH;
    MoveWindow(btn_switch_, kMargin, row1, kBtnW, kRowH, TRUE);
    MoveWindow(btn_kill_, kMargin + kBtnW + 6, row1, kBtnW, kRowH, TRUE);

    const int row2    = row1 + kRowH + 8;
    const int label_w = 34;
    const int edit_w  = w - 2 * kMargin - label_w - kBtnW - 12 - 170;
    MoveWindow(run_label_, kMargin, row2 + 4, label_w, kRowH - 6, TRUE);
    MoveWindow(run_edit_, kMargin + label_w, row2,
               edit_w > 80 ? edit_w : 80, kRowH - 2, TRUE);
    MoveWindow(btn_run_, kMargin + label_w + (edit_w > 80 ? edit_w : 80) + 6,
               row2, kBtnW, kRowH, TRUE);
    MoveWindow(status_, kMargin + label_w + (edit_w > 80 ? edit_w : 80) +
               kBtnW + 12, row2 + 4, 164, kRowH - 6, TRUE);
}

void TaskManagerWindow::OnTick() {
    auto& svc = emu_.Get<CerfVirtTaskManager>();
    svc.RequestProcessList();

    if (auto r = svc.TakeActionResult()) {
        wchar_t buf[96];
        if (r->code == CerfVirt::kTmCmdRun) {
            if (r->ticket == run_ticket_) {
                if (r->ok) {
                    SetStatus(L"success", kClrOk);
                } else {
                    _snwprintf_s(buf, _TRUNCATE, L"failed (error %u)",
                                 r->guest_err);
                    SetStatus(buf, kClrFail);
                }
            }
        } else if (r->code == CerfVirt::kTmCmdList) {
            if (!r->ok) {
                _snwprintf_s(buf, _TRUNCATE,
                             L"process list failed (error %u)", r->guest_err);
                SetStatus(buf, kClrFail);
            }
        } else if (r->ok) {
            _snwprintf_s(buf, _TRUNCATE, L"%s: done", ActionName(r->code));
            SetStatus(buf, kClrOk);
        } else {
            _snwprintf_s(buf, _TRUNCATE, L"%s failed (error %u)",
                         ActionName(r->code), r->guest_err);
            SetStatus(buf, kClrFail);
        }
    }

    Repopulate();
}

void TaskManagerWindow::Repopulate() {
    auto snap = emu_.Get<CerfVirtTaskManager>().GetSnapshot();
    if (snap.gen == shown_gen_) return;

    /* Rebuild only on real content change — the snapshot gen bumps on every
       LIST round-trip (2 Hz) and a constant rebuild flickers the selection. */
    const bool first = shown_gen_ == 0;
    shown_gen_ = snap.gen;
    if (first) SetStatus(L"", kClrInfo);   /* replaces "Waiting for guest…" */
    if (!first && SameProcs(snap.procs, shown_procs_)) return;
    shown_procs_ = snap.procs;

    uint32_t sel_pid = 0;
    const bool had_sel = SelectedPid(&sel_pid);

    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list_);
    wchar_t buf[64];
    for (int i = 0; i < (int)snap.procs.size(); ++i) {
        const auto& p = snap.procs[(size_t)i];
        LVITEMW it = {};
        it.mask    = LVIF_TEXT | LVIF_PARAM;
        it.iItem   = i;
        it.pszText = (LPWSTR)p.name.c_str();
        it.lParam  = (LPARAM)p.pid;
        ListView_InsertItem(list_, &it);
        _snwprintf_s(buf, _TRUNCATE, L"0x%08X", p.pid);
        ListView_SetItemText(list_, i, 1, buf);
        _snwprintf_s(buf, _TRUNCATE, L"0x%08X", p.parent_pid);
        ListView_SetItemText(list_, i, 2, buf);
        _snwprintf_s(buf, _TRUNCATE, L"%u", p.thread_count);
        ListView_SetItemText(list_, i, 3, buf);
        _snwprintf_s(buf, _TRUNCATE, L"%d", p.base_priority);
        ListView_SetItemText(list_, i, 4, buf);
        _snwprintf_s(buf, _TRUNCATE, L"0x%08X", p.mem_base);
        ListView_SetItemText(list_, i, 5, buf);
        if (had_sel && p.pid == sel_pid) {
            ListView_SetItemState(list_, i, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, TRUE);

    wchar_t title[96];
    if (snap.guest_total > snap.procs.size())
        _snwprintf_s(title, _TRUNCATE,
                     L"Guest Task Manager — %u of %u processes",
                     (unsigned)snap.procs.size(), snap.guest_total);
    else
        _snwprintf_s(title, _TRUNCATE, L"Guest Task Manager — %u processes",
                     (unsigned)snap.procs.size());
    SetWindowTextW(hwnd_, title);
}

bool TaskManagerWindow::SelectedPid(uint32_t* pid) const {
    const int i = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    if (i < 0) return false;
    LVITEMW it = {};
    it.mask  = LVIF_PARAM;
    it.iItem = i;
    if (!ListView_GetItem(list_, &it)) return false;
    *pid = (uint32_t)it.lParam;
    return true;
}

void TaskManagerWindow::DoSwitchTo() {
    uint32_t pid;
    if (!SelectedPid(&pid)) { SetStatus(L"no process selected", kClrFail); return; }
    emu_.Get<CerfVirtTaskManager>().RequestSwitchTo(pid);
    SetStatus(L"switching…", kClrInfo);
}

void TaskManagerWindow::DoKill() {
    uint32_t pid;
    if (!SelectedPid(&pid)) { SetStatus(L"no process selected", kClrFail); return; }
    emu_.Get<CerfVirtTaskManager>().RequestKill(pid);
    SetStatus(L"killing…", kClrInfo);
}

void TaskManagerWindow::DoRun() {
    wchar_t buf[CerfVirt::kTmRunMaxWchars + 1] = { 0 };
    GetWindowTextW(run_edit_, buf, (int)(CerfVirt::kTmRunMaxWchars + 1));
    const std::wstring cmd = buf;
    if (cmd.empty()) { SetStatus(L"enter a command", kClrFail); return; }
    run_ticket_ = emu_.Get<CerfVirtTaskManager>().RequestRun(cmd);
    if (run_ticket_ == 0) { SetStatus(L"failed", kClrFail); return; }
    SetStatus(L"running…", kClrInfo);
}

void TaskManagerWindow::ShowRowMenu(int item, POINT screen_pt) {
    if (item >= 0) {
        ListView_SetItemState(list_, item, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_ROW_SWITCH, L"Switch to");
    AppendMenuW(m, MF_STRING, IDM_ROW_KILL, L"Kill");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd_,
                   nullptr);
    DestroyMenu(m);
}

void TaskManagerWindow::SetStatus(const std::wstring& text, COLORREF clr) {
    status_clr_ = clr;
    SetWindowTextW(status_, text.c_str());
    InvalidateRect(status_, nullptr, TRUE);
}

LRESULT CALLBACK TaskManagerWindow::RunEditProcStatic(HWND hwnd, UINT msg,
                                                      WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<TaskManagerWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(IDC_RUN, BN_CLICKED), 0);
        return 0;
    }
    if (msg == WM_CHAR && wp == L'\r') return 0;   /* swallow the beep */
    return CallWindowProcW(self->run_edit_base_proc_, hwnd, msg, wp, lp);
}

LRESULT CALLBACK TaskManagerWindow::WndProcStatic(HWND hwnd, UINT msg,
                                                  WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* self = reinterpret_cast<TaskManagerWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(hwnd, msg, wp, lp);
}

LRESULT TaskManagerWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            LayoutControls();
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize = { 480, 320 };
            return 0;
        }

        case WM_TIMER:
            if (wp == kRefreshTimerId) OnTick();
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_RUN:         DoRun();      return 0;
                case IDC_SWITCH:
                case IDM_ROW_SWITCH:  DoSwitchTo(); return 0;
                case IDC_KILL:
                case IDM_ROW_KILL:    DoKill();     return 0;
            }
            return 0;

        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lp);
            if (hdr->hwndFrom == list_ && hdr->code == NM_RCLICK) {
                auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                POINT pt = ia->ptAction;
                ClientToScreen(list_, &pt);
                ShowRowMenu(ia->iItem, pt);
                return 0;
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            auto& dm = emu_.Get<HostDarkMode>();
            if ((HWND)lp == status_) {
                HDC dc = (HDC)wp;
                SetTextColor(dc, status_clr_);
                SetBkMode(dc, TRANSPARENT);
                if (dm.IsDark()) {
                    SetBkColor(dc, dm.BgColor());
                    return (LRESULT)dm.BgBrush();
                }
                return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
            }
            LRESULT br;
            if (dm.HandleCtlColor(msg, wp, br)) return br;
            break;
        }

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            LRESULT br;
            if (emu_.Get<HostDarkMode>().HandleCtlColor(msg, wp, br)) return br;
            break;
        }

        case WM_ERASEBKGND:
            if (emu_.Get<HostDarkMode>().EraseBackground((HDC)wp, hwnd))
                return 1;
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, kRefreshTimerId);
            hwnd_ = nullptr;
            list_ = run_edit_ = btn_run_ = btn_switch_ = btn_kill_ = nullptr;
            run_label_ = status_ = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
