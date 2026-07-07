/* Wizard step 1: CPU / board preset selection in a full-height listbox. */

#include "romdump.h"

#define ID_PRESET_TITLE 211

void StepPresetCreate(AppState* st, HINSTANCE hi) {
    int i;
    CreateWindowExW(0, L"STATIC", L"Select the device CPU / board:",
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    0, 0, 0, 0, st->hwnd, (HMENU)ID_PRESET_TITLE, hi, NULL);
    CreateWindowExW(0, L"LISTBOX", NULL,
                    WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_TABSTOP |
                    LBS_NOTIFY | LBS_HASSTRINGS,
                    0, 0, 0, 0, st->hwnd, (HMENU)ID_PRESETLIST, hi, NULL);
    for (i = 0; i < kNumPresets; ++i)
        SendDlgItemMessage(st->hwnd, ID_PRESETLIST, LB_ADDSTRING, 0,
                           (LPARAM)kPresets[i].name);
    SendDlgItemMessage(st->hwnd, ID_PRESETLIST, LB_SETCURSEL, 0, 0);
}

void StepPresetShow(AppState* st, BOOL show) {
    int cmd = show ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(st->hwnd, ID_PRESET_TITLE), cmd);
    ShowWindow(GetDlgItem(st->hwnd, ID_PRESETLIST),   cmd);
}

/* The listbox fills the content area and scrolls its own entries, so the outer
   wizard scrollbar is not needed here: return the view height. */
int StepPresetLayout(AppState* st, RECT area) {
    int m = 6, x = area.left + m, w = area.right - area.left - 2 * m;
    int top = area.top + 4;
    if (w < 40) w = 40;
    MoveWindow(GetDlgItem(st->hwnd, ID_PRESET_TITLE), x, top, w, 18, TRUE);
    MoveWindow(GetDlgItem(st->hwnd, ID_PRESETLIST),
               x, top + 22, w, (area.bottom - (top + 22)) - m, TRUE);
    return area.bottom - area.top;
}

BOOL StepPresetCommand(AppState* st, WPARAM wp, LPARAM lp) {
    (void)lp;
    /* Double-click a preset acts as Next. */
    if (LOWORD(wp) == ID_PRESETLIST && HIWORD(wp) == LBN_DBLCLK) {
        if (StepPresetOnNext(st))
            SendMessageW(st->hwnd, WM_COMMAND,
                         MAKEWPARAM(ID_NAV, BN_CLICKED),
                         (LPARAM)GetDlgItem(st->hwnd, ID_NAV));
        return TRUE;
    }
    return FALSE;
}

BOOL StepPresetOnNext(AppState* st) {
    int sel = (int)SendDlgItemMessage(st->hwnd, ID_PRESETLIST, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= kNumPresets) sel = 0;
    st->preset_index = sel;
    return TRUE;
}
