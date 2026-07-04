/* CERF physical-memory ROM dumper for Windows CE.

   On a memory-mapped/XIP device the OS ROM is NOR flash on a static
   chip-select at a fixed physical address (PA 0 on most ARM boards - the
   reset vector). It executes in place, not copied to DRAM, so "the ROM" is
   the bytes at that base upward.

   The sweep walks the selected physical range in 1 MB windows, maps each
   with VirtualCopy(...PAGE_PHYSICAL), and reads it page-by-page under SEH so
   an unpopulated bank's data-abort fills 0xFF rather than aborting. Output
   is linear: file offset == physical offset from base, so an offline tool
   finds the CE ROM signature with no fix-up.

   VirtualCopy/PAGE_PHYSICAL is the standard CE physical-access API
   (CE 2.11 .. CE 7, any CPU); only base/size differ per device. NAND-boot
   devices whose image is not memory-mapped need a different approach.

   This file owns the window, controls and message loop; the dump worker is
   in dump.cpp and the progress rendering in paint.cpp.

   Plain ARMV4, coredll-only, PE stamped windowsce,2.11. */

#include "romdump.h"

#include "cerf_icon_data.h"

#define DEFAULT_PATH L"\\Storage Card\\dump.bin"

#define ID_PRESETLBL 110
#define ID_PRESET    105
#define ID_PATHLBL   104
#define ID_EDIT      101
#define ID_BASELBL   111
#define ID_BASE      106
#define ID_SIZELBL   112
#define ID_SIZE      107
#define ID_SEGCHK    108
#define ID_SEGSIZELBL 113
#define ID_SEGSIZE   109
#define ID_GO        102
#define ID_EXIT      103

/* Preset table is CPU-arch specific (build_ce_app.ps1 defines MIPS for -Arch mips,
   ARM/_ARM_ for -Arch arm). Custom (base/size entered by hand) is common to both. */
typedef struct { LPCWSTR name; DWORD base; DWORD size_mb; int custom; } Preset;
static const Preset kPresets[] = {
#if defined(MIPS)
    /* TX39 (R3000) OS ROM = chip-select CS0, physical 0x11000000, 4 MB window
       ("CS0(ROM)", NetBSD hpcmips tx39biureg.h: TX39_SYSADDR_CS0 / _CS_SIZE). */
    { L"PR31700 (Nino)", 0x11000000u, 4, 0 },
    { L"PR31500 (Velo)", 0x11000000u, 4, 0 },
#else
    /* base = PA 0 for all (reset vector); size_mb covers the SoC's static banks
       per its CERF page-table builder under cerf/boards. Over-dump is free -
       unmapped space SEH-fills 0xFF. */
    { L"SA-11x0",     0x00000000u, 512, 0 },  /* banks 128 MB x4  */
    { L"PXA25x/27x",  0x00000000u, 384, 0 },  /* CS 64 MB x6      */
    { L"S3C2410",     0x00000000u, 768, 0 },  /* nGCS up to SDRAM */
    { L"ARM720",      0x00000000u, 256, 0 },  /* flash + ASIC     */
#endif
    { L"Custom",      0x00000000u,   0, 1 },
};
#define NUM_PRESETS (int)(sizeof(kPresets) / sizeof(kPresets[0]))

static DWORD ParseUHex(LPCWSTR s) {
    DWORD v = 0;
    if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) s += 2;
    for (; *s; ++s) {
        WCHAR c = *s;
        DWORD d;
        if      (c >= L'0' && c <= L'9') d = (DWORD)(c - L'0');
        else if (c >= L'a' && c <= L'f') d = (DWORD)(10 + c - L'a');
        else if (c >= L'A' && c <= L'F') d = (DWORD)(10 + c - L'A');
        else break;
        v = (v << 4) | d;
    }
    return v;
}

static DWORD ParseUDec(LPCWSTR s) {
    DWORD v = 0;
    for (; *s >= L'0' && *s <= L'9'; ++s) v = v * 10 + (DWORD)(*s - L'0');
    return v;
}

/* Fill Base/Size from preset idx and enable them only for Custom. */
static void ApplyPreset(HWND hwnd, int idx) {
    WCHAR buf[32];
    BOOL  custom;
    if (idx < 0 || idx >= NUM_PRESETS) return;
    custom = kPresets[idx].custom;
    if (!custom) {
        wsprintfW(buf, L"0x%08X", kPresets[idx].base);
        SetDlgItemTextW(hwnd, ID_BASE, buf);
        wsprintfW(buf, L"%u", kPresets[idx].size_mb);
        SetDlgItemTextW(hwnd, ID_SIZE, buf);
    }
    EnableWindow(GetDlgItem(hwnd, ID_BASE), custom);
    EnableWindow(GetDlgItem(hwnd, ID_SIZE), custom);
}

/* Segment-size box is editable only while the Segmented box is checked. */
static void SyncSegEnable(HWND hwnd) {
    EnableWindow(GetDlgItem(hwnd, ID_SEGSIZE),
                 SendDlgItemMessageW(hwnd, ID_SEGCHK, BM_GETCHECK, 0, 0)
                 == BST_CHECKED);
}

/* One control per row, label left, control fills the rest - fits a 240-wide
   PocketPC screen and stretches on wider ones. The top two rows (preset,
   file) stop short of the top-right logo; rows below it use full width. */
static void LayoutControls(HWND hwnd) {
    RECT rc;
    int  m = 6, lbl = 56, cx = 64, narrowR, fullR;
    GetClientRect(hwnd, &rc);
    fullR   = rc.right - m;
    narrowR = rc.right - CERF_ICON_W - 2 * m;
    if (narrowR < cx + 48) narrowR = cx + 48;
    if (fullR   < cx + 48) fullR   = cx + 48;

    MoveWindow(GetDlgItem(hwnd, ID_PRESETLBL), m,  10, lbl, 16, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_PRESET),    cx,  8, narrowR - cx, 160, TRUE);

    MoveWindow(GetDlgItem(hwnd, ID_PATHLBL),   m,  38, lbl, 16, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_EDIT),      cx, 36, narrowR - cx, 20, TRUE);

    MoveWindow(GetDlgItem(hwnd, ID_BASELBL),   m,  66, lbl, 16, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_BASE),      cx, 64, fullR - cx, 20, TRUE);

    MoveWindow(GetDlgItem(hwnd, ID_SIZELBL),   m,  94, lbl, 16, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_SIZE),      cx, 92, fullR - cx, 20, TRUE);

    MoveWindow(GetDlgItem(hwnd, ID_SEGCHK),     m,     118, 88, 20, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_SEGSIZELBL), m+96,  120, 52, 16, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_SEGSIZE),    m+152, 118, fullR - (m + 152), 20, TRUE);

    MoveWindow(GetDlgItem(hwnd, ID_GO),        m,    146, 64, 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_EXIT),      m+72, 146, 64, 24, TRUE);
}

/* Read target path + base/size from the controls and launch the worker. */
static void StartDump(HWND hwnd, DumpState* st) {
    WCHAR num[32];
    DWORD size_mb, tid;

    GetDlgItemTextW(hwnd, ID_EDIT, st->outpath, MAX_PATH);
    if (st->outpath[0] == L'\0') {
        MessageBoxW(hwnd, L"Enter a target file path first.",
                    L"CERF ROM dumper", MB_OK | MB_ICONEXCLAMATION);
        return;
    }

    GetDlgItemTextW(hwnd, ID_BASE, num, 32);
    st->base = ParseUHex(num);
    GetDlgItemTextW(hwnd, ID_SIZE, num, 32);
    size_mb = ParseUDec(num);
    if (size_mb == 0) {
        MessageBoxW(hwnd, L"Size must be at least 1 MB.",
                    L"CERF ROM dumper", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    st->length = size_mb << 20;

    st->segmented = SendDlgItemMessageW(hwnd, ID_SEGCHK, BM_GETCHECK, 0, 0)
                    == BST_CHECKED;
    if (st->segmented) {
        DWORD seg_mb;
        GetDlgItemTextW(hwnd, ID_SEGSIZE, num, 32);
        seg_mb = ParseUDec(num);
        if (seg_mb == 0) {
            MessageBoxW(hwnd, L"Segment size must be at least 1 MB.",
                        L"CERF ROM dumper", MB_OK | MB_ICONEXCLAMATION);
            return;
        }
        st->seg_bytes = seg_mb << 20;
    } else {
        st->seg_bytes = 0;
    }

    st->cancel = 0; st->finished = 0; st->ok = 0;
    st->bytes_done = 0; st->fault_pages = 0; st->cur_pa = 0; st->err[0] = L'\0';
    st->seg_index = 0; st->segs_written = 0; st->seg_continue = 0;
    if (st->seg_event) ResetEvent(st->seg_event);

    EnableWindow(GetDlgItem(hwnd, ID_GO),     FALSE);
    EnableWindow(GetDlgItem(hwnd, ID_EDIT),   FALSE);
    EnableWindow(GetDlgItem(hwnd, ID_PRESET), FALSE);
    EnableWindow(GetDlgItem(hwnd, ID_BASE),   FALSE);
    EnableWindow(GetDlgItem(hwnd, ID_SIZE),   FALSE);
    EnableWindow(GetDlgItem(hwnd, ID_SEGCHK), FALSE);
    EnableWindow(GetDlgItem(hwnd, ID_SEGSIZE), FALSE);
    st->running = 1;

    st->thread = CreateThread(NULL, 0, DumpThread, st, 0, &tid);
    if (!st->thread) {
        st->running = 0;
        EnableWindow(GetDlgItem(hwnd, ID_GO),     TRUE);
        EnableWindow(GetDlgItem(hwnd, ID_EDIT),   TRUE);
        EnableWindow(GetDlgItem(hwnd, ID_PRESET), TRUE);
        EnableWindow(GetDlgItem(hwnd, ID_SEGCHK), TRUE);
        SyncSegEnable(hwnd);
        ApplyPreset(hwnd, (int)SendDlgItemMessageW(hwnd, ID_PRESET,
                                                   CB_GETCURSEL, 0, 0));
        MessageBoxW(hwnd, L"Failed to start dump thread.",
                    L"CERF ROM dumper", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    SetTimer(hwnd, 1, 250, NULL);
    InvalidateRect(hwnd, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DumpState* st = (DumpState*)GetWindowLongW(hwnd, GWL_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        DumpState* s = (DumpState*)cs->lpCreateParams;
        HINSTANCE hi = cs->hInstance;
        int i;
        SetWindowLongW(hwnd, GWL_USERDATA, (LONG)s);
        s->hwnd = hwnd;

        CreateWindowExW(0, L"STATIC", L"Preset:",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        0, 0, 0, 0, hwnd, (HMENU)ID_PRESETLBL, hi, NULL);
        CreateWindowExW(0, L"COMBOBOX", NULL,
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                        WS_VSCROLL,
                        0, 0, 0, 0, hwnd, (HMENU)ID_PRESET, hi, NULL);
        for (i = 0; i < NUM_PRESETS; ++i)
            SendDlgItemMessageW(hwnd, ID_PRESET, CB_ADDSTRING, 0,
                                (LPARAM)kPresets[i].name);
        SendDlgItemMessageW(hwnd, ID_PRESET, CB_SETCURSEL, 0, 0);

        CreateWindowExW(0, L"STATIC", L"File:",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        0, 0, 0, 0, hwnd, (HMENU)ID_PATHLBL, hi, NULL);
        CreateWindowExW(0, L"EDIT", DEFAULT_PATH,
                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                        ES_AUTOHSCROLL,
                        0, 0, 0, 0, hwnd, (HMENU)ID_EDIT, hi, NULL);

        CreateWindowExW(0, L"STATIC", L"Base hex:",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        0, 0, 0, 0, hwnd, (HMENU)ID_BASELBL, hi, NULL);
        CreateWindowExW(0, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                        ES_AUTOHSCROLL,
                        0, 0, 0, 0, hwnd, (HMENU)ID_BASE, hi, NULL);
        CreateWindowExW(0, L"STATIC", L"Size MB:",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        0, 0, 0, 0, hwnd, (HMENU)ID_SIZELBL, hi, NULL);
        CreateWindowExW(0, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                        ES_NUMBER | ES_AUTOHSCROLL,
                        0, 0, 0, 0, hwnd, (HMENU)ID_SIZE, hi, NULL);

        CreateWindowExW(0, L"BUTTON", L"Segmented",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                        0, 0, 0, 0, hwnd, (HMENU)ID_SEGCHK, hi, NULL);
        CreateWindowExW(0, L"STATIC", L"Seg MB:",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        0, 0, 0, 0, hwnd, (HMENU)ID_SEGSIZELBL, hi, NULL);
        CreateWindowExW(0, L"EDIT", L"1",
                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                        ES_NUMBER | ES_AUTOHSCROLL,
                        0, 0, 0, 0, hwnd, (HMENU)ID_SEGSIZE, hi, NULL);

        CreateWindowExW(0, L"BUTTON", L"GO",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        0, 0, 0, 0, hwnd, (HMENU)ID_GO, hi, NULL);
        CreateWindowExW(0, L"BUTTON", L"Exit",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        0, 0, 0, 0, hwnd, (HMENU)ID_EXIT, hi, NULL);

        LayoutControls(hwnd);
        ApplyPreset(hwnd, 0);
        SyncSegEnable(hwnd);
        return 0;
    }

    case WM_SIZE:
        LayoutControls(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_COMMAND:
        if (!st) return 0;
        if (LOWORD(wp) == ID_PRESET && HIWORD(wp) == CBN_SELCHANGE) {
            ApplyPreset(hwnd, (int)SendDlgItemMessageW(hwnd, ID_PRESET,
                                                       CB_GETCURSEL, 0, 0));
            return 0;
        }
        if (LOWORD(wp) == ID_SEGCHK && HIWORD(wp) == BN_CLICKED) {
            SyncSegEnable(hwnd);
            return 0;
        }
        if (LOWORD(wp) == ID_GO) {
            if (!st->running) StartDump(hwnd, st);
            return 0;
        }
        if (LOWORD(wp) == ID_EXIT) {
            st->cancel = 1;                       /* let the worker bail */
            if (st->seg_event) SetEvent(st->seg_event);  /* wake a segment wait */
            if (st->thread) WaitForSingleObject(st->thread, 3000);
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_TIMER:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_PAINT:
        if (st) PaintProgress(hwnd, st);
        return 0;

    case WM_APP_SEGMENT:
        if (st) {
            WCHAR msg[MAX_PATH + 128];
            DWORD n = st->seg_index;
            wsprintfW(msg,
                      L"Segment %03u written (%u MB):\n%s.%03u\n\n"
                      L"Copy it off the device if you need to, then continue.\n\n"
                      L"Write the next segment (.%03u)?",
                      (unsigned)n, (unsigned)(st->seg_bytes >> 20),
                      st->outpath, (unsigned)n, (unsigned)(n + 1));
            st->seg_continue =
                (MessageBoxW(hwnd, msg, L"CERF ROM dumper - segmented",
                             MB_YESNO | MB_ICONQUESTION) == IDYES) ? 1 : 0;
            SetEvent(st->seg_event);
        }
        return 0;

    case WM_APP_DONE:
        KillTimer(hwnd, 1);
        if (st && st->thread) { CloseHandle(st->thread); st->thread = NULL; }
        if (st) st->running = 0;
        EnableWindow(GetDlgItem(hwnd, ID_GO),     TRUE);
        EnableWindow(GetDlgItem(hwnd, ID_EDIT),   TRUE);
        EnableWindow(GetDlgItem(hwnd, ID_PRESET), TRUE);
        EnableWindow(GetDlgItem(hwnd, ID_SEGCHK), TRUE);
        SyncSegEnable(hwnd);
        ApplyPreset(hwnd, (int)SendDlgItemMessageW(hwnd, ID_PRESET,
                                                   CB_GETCURSEL, 0, 0));
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
        if (st && st->ok && st->segmented) {
            WCHAR done[MAX_PATH + 128];
            wsprintfW(done,
                      L"Dump complete.\n\n%u segment file(s):\n%s.001 ... .%03u\n"
                      L"%u MB total, %u pages 0xFF-filled.",
                      (unsigned)st->segs_written, st->outpath,
                      (unsigned)st->segs_written, (unsigned)(st->bytes_done >> 20),
                      (unsigned)st->fault_pages);
            MessageBoxW(hwnd, done, L"CERF ROM dumper", MB_OK | MB_ICONINFORMATION);
        } else if (st && st->ok) {
            WCHAR done[MAX_PATH + 96];
            wsprintfW(done,
                      L"Dump complete.\n\n%s\n%u MB written, %u pages 0xFF-filled.",
                      st->outpath, (unsigned)(st->bytes_done >> 20),
                      (unsigned)st->fault_pages);
            MessageBoxW(hwnd, done, L"CERF ROM dumper", MB_OK | MB_ICONINFORMATION);
        } else if (st && st->err[0]) {
            MessageBoxW(hwnd, st->err, L"CERF ROM dumper", MB_OK | MB_ICONEXCLAMATION);
        } else if (st && st->segmented && st->segs_written) {
            WCHAR done[MAX_PATH + 96];
            wsprintfW(done,
                      L"Stopped after %u segment file(s):\n%s.001 ... .%03u",
                      (unsigned)st->segs_written, st->outpath,
                      (unsigned)st->segs_written);
            MessageBoxW(hwnd, done, L"CERF ROM dumper", MB_OK | MB_ICONINFORMATION);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* extern "C": /entry:WinMain needs an unmangled symbol; this file is C++. */
extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev,
                              LPWSTR cmd, int show) {
    static DumpState st;
    WNDCLASSW wc;
    HWND      hwnd;
    MSG       m;

    (void)hPrev; (void)cmd;

    st.seg_event = CreateEventW(NULL, FALSE, FALSE, NULL);  /* auto-reset, off */
    if (!st.seg_event) return 1;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"CerfRomDump";
    if (!RegisterClassW(&wc)) return 1;

    /* Pass &st as the create param so WM_CREATE can wire up the controls. */
    hwnd = CreateWindowExW(0, L"CerfRomDump", L"CERF ROM dumper",
                           WS_VISIBLE | WS_BORDER,
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                           NULL, NULL, hInstance, &st);
    if (!hwnd) return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
