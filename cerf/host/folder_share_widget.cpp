#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../core/folder_share_config.h"
#include "../core/service.h"
#include "host_dark_mode.h"
#include "host_gdiplus.h"
#include "host_widget.h"
#include "host_widget_registry.h"
#include "host_window.h"

#include <string>
#include <vector>

namespace {

constexpr wchar_t kDlgClass[] = L"CerfFolderShareDlg";
constexpr COLORREF kFolderClr   = RGB(232, 196, 92);  /* folder yellow (on) */
constexpr COLORREF kDisabledClr = RGB(128, 128, 128);  /* grayscale (off) */

enum : int { IDC_ENABLE = 1001, IDC_PATH = 1002, IDC_BROWSE = 1003 };

/* Status-bar UI for guest-additions folder sharing: shows on/off state and
   opens the configure dialog. The state itself lives in FolderShareConfig. */
class FolderShareWidget : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &FolderShareWidget::DlgProcStatic;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kDlgClass;
        RegisterClassExW(&wc);   /* ERROR_CLASS_ALREADY_EXISTS is benign */

        emu_.Get<HostWidgetRegistry>().Register(this);
    }
    /* HostWidget */
    std::wstring WidgetName() const override { return L"Shared folder"; }
    WidgetGroup  Group() const override { return WidgetGroup::Storage; }
    std::wstring Tooltip() const override {
        auto& cfg = emu_.Get<FolderShareConfig>();
        if (!cfg.Enabled())
            return L"Shared folder: off - click to configure";
        return L"Shared folder: " + cfg.HostRoot();
    }
    void OnPrimaryAction() override { ShowConfigDialog(); }
    std::vector<WidgetMenuItem> BuildMenu() override {
        auto& cfg = emu_.Get<FolderShareConfig>();
        const bool on = cfg.Enabled();
        const std::wstring root = cfg.HostRoot();

        std::vector<WidgetMenuItem> items;

        /* Toggle: enabling needs a folder, so it's grayed when none is set
           (FolderShareConfig forces disabled on an empty root anyway). */
        WidgetMenuItem toggle;
        toggle.label    = L"Share folder with guest";
        toggle.checked  = on;
        toggle.enabled  = on || !root.empty();
        toggle.on_click = [this] { ToggleEnabled(); };
        items.push_back(std::move(toggle));

        WidgetMenuItem change;
        change.label    = root.empty() ? L"Choose folder and enable…"
                                       : L"Change folder and enable…";
        change.on_click = [this] { ChangeFolder(); };
        items.push_back(std::move(change));

        items.push_back(WidgetMenuItem{});  /* separator */

        WidgetMenuItem configure;
        configure.label    = L"Configure shared folder…";
        configure.on_click = [this] { ShowConfigDialog(); };
        items.push_back(std::move(configure));

        return items;
    }
    void DrawIcon(HDC dc, const RECT& box) const override {
        const COLORREF clr = emu_.Get<FolderShareConfig>().Enabled()
                                 ? kFolderClr : kDisabledClr;
        const int cx = (box.left + box.right) / 2;
        const int cy = (box.top + box.bottom) / 2;
        const POINT folder[6] = {
            { cx - 8, cy + 6 }, { cx - 8, cy - 6 }, { cx - 2, cy - 6 },
            { cx,     cy - 3 }, { cx + 8, cy - 3 }, { cx + 8, cy + 6 },
        };
        emu_.Get<HostGdiPlus>().FillPolygonAA(dc, folder, 6, clr, clr);
    }
    bool PollDirty() override {
        const bool on = emu_.Get<FolderShareConfig>().Enabled();
        if (on == drawn_enabled_) return false;
        drawn_enabled_ = on;
        return true;
    }

private:
    void ShowConfigDialog();
    void Browse(HWND dlg);
    void ApplyFromControls(HWND dlg);
    /* Modal host folder picker; returns the chosen path, empty if cancelled. */
    std::wstring PickFolder(HWND owner, const std::wstring& initial);
    /* Menu shortcuts - flip enabled in place / repoint the shared folder. */
    void ToggleEnabled();
    void ChangeFolder();
    static LRESULT CALLBACK DlgProcStatic(HWND, UINT, WPARAM, LPARAM);

    bool drawn_enabled_ = false;
};

std::wstring FolderShareWidget::PickFolder(HWND owner,
                                           const std::wstring& initial) {
    std::wstring result;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IFileOpenDialog* picker = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker)))) {
        DWORD opts = 0;
        picker->GetOptions(&opts);
        picker->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                           FOS_PATHMUSTEXIST);
        picker->SetTitle(L"Choose a host folder to share with the guest");

        if (!initial.empty()) {
            IShellItem* start = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr,
                                                      IID_PPV_ARGS(&start)))) {
                picker->SetFolder(start);
                start->Release();
            }
        }

        if (SUCCEEDED(picker->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(picker->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        picker->Release();
    }

    if (co == S_OK || co == S_FALSE) CoUninitialize();
    return result;
}

void FolderShareWidget::Browse(HWND dlg) {
    wchar_t cur[MAX_PATH] = { 0 };
    GetDlgItemTextW(dlg, IDC_PATH, cur, MAX_PATH);
    const std::wstring picked = PickFolder(dlg, cur);
    if (!picked.empty()) SetDlgItemTextW(dlg, IDC_PATH, picked.c_str());
}

void FolderShareWidget::ToggleEnabled() {
    auto& cfg = emu_.Get<FolderShareConfig>();
    cfg.Set(!cfg.Enabled(), cfg.HostRoot(), L"");
}

void FolderShareWidget::ChangeFolder() {
    auto& cfg = emu_.Get<FolderShareConfig>();
    const std::wstring picked =
        PickFolder(emu_.Get<HostWindow>().Hwnd(), cfg.HostRoot());
    if (picked.empty()) return;            /* cancelled - keep current folder */
    cfg.Set(/*enabled=*/true, picked, L"");  /* repoint and enable in one step */
}

void FolderShareWidget::ApplyFromControls(HWND dlg) {
    wchar_t buf[MAX_PATH] = { 0 };
    GetDlgItemTextW(dlg, IDC_PATH, buf, MAX_PATH);
    const bool want = IsDlgButtonChecked(dlg, IDC_ENABLE) == BST_CHECKED;
    /* Empty mount-point keeps the config default; FolderShareConfig forces
       disabled when the host root is empty. */
    emu_.Get<FolderShareConfig>().Set(want, buf, L"");
}

void FolderShareWidget::ShowConfigDialog() {
    HWND owner = emu_.Get<HostWindow>().Hwnd();
    auto& cfg = emu_.Get<FolderShareConfig>();
    const std::wstring host_root = cfg.HostRoot();

    /* Size from the desired CLIENT rect so the caption doesn't eat the bottom
       row of controls (AdjustWindowRectEx adds the non-client frame). */
    const DWORD style   = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exstyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    RECT wr = { 0, 0, 420, 180 };
    AdjustWindowRectEx(&wr, style, FALSE, exstyle);
    const int win_w = wr.right - wr.left;
    const int win_h = wr.bottom - wr.top;
    HWND dlg = CreateWindowExW(exstyle, kDlgClass, L"Shared folder", style,
                               0, 0, win_w, win_h, owner, nullptr,
                               GetModuleHandleW(nullptr), this);
    if (!dlg) return;

    RECT orc;
    GetWindowRect(owner, &orc);
    const int x = orc.left + ((orc.right - orc.left) - win_w) / 2;
    const int y = orc.top  + ((orc.bottom - orc.top) - win_h) / 2;
    SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                  int cx, int cy, int cw, int ch, int id) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               cx, cy, cw, ch, dlg, (HMENU)(INT_PTR)id,
                               GetModuleHandleW(nullptr), nullptr);
    };
    mk(L"BUTTON", L"Enable shared folder",
       BS_AUTOCHECKBOX | WS_TABSTOP, 16, 14, 280, 22, IDC_ENABLE);
    mk(L"STATIC", L"Host folder:", 0, 16, 48, 80, 18, 0);
    mk(L"EDIT", host_root.c_str(),
       WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 16, 68, 300, 24, IDC_PATH);
    mk(L"BUTTON", L"Browse…", BS_PUSHBUTTON | WS_TABSTOP,
       324, 67, 80, 26, IDC_BROWSE);
    mk(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP,
       228, 128, 84, 28, IDOK);
    mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP,
       320, 128, 84, 28, IDCANCEL);

    CheckDlgButton(dlg, IDC_ENABLE,
                   cfg.Enabled() ? BST_CHECKED : BST_UNCHECKED);

    /* Dark title bar + dark-theme every child + modern UI font, one call. */
    emu_.Get<HostDarkMode>().ApplyToDialog(dlg);

    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(GetDlgItem(dlg, IDC_PATH));

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

LRESULT CALLBACK FolderShareWidget::DlgProcStatic(HWND hwnd, UINT msg,
                                                  WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* self = reinterpret_cast<FolderShareWidget*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_BROWSE: self->Browse(hwnd);            return 0;
                case IDOK:       self->ApplyFromControls(hwnd);
                                 DestroyWindow(hwnd);           return 0;
                case IDCANCEL:   DestroyWindow(hwnd);           return 0;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_ERASEBKGND:
            if (self->emu_.Get<HostDarkMode>().EraseBackground((HDC)wp, hwnd))
                return 1;
            break;
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT: {
            LRESULT br;
            if (self->emu_.Get<HostDarkMode>().HandleCtlColor(msg, wp, br))
                return br;
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  /* namespace */

REGISTER_SERVICE(FolderShareWidget);
