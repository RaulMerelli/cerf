/* Progress-window rendering: CERF logo + status text + progress bar. */

#include "romdump.h"

#include "cerf_icon_data.h"

/* CE GDI has no TextOut, only ExtTextOutW. */
static void DrawLineW(HDC dc, int x, int y, LPCWSTR s) {
    ExtTextOutW(dc, x, y, 0, NULL, s, lstrlenW(s), NULL);
}

/* Blit the embedded CERF logo. CE 2.11 GDI has no StretchDIBits/
   SetDIBitsToDevice, so go through a DIBSection-backed memory DC + BitBlt. */
static void DrawCerfIcon(HDC dc, int x, int y) {
    BITMAPINFO bi;
    void*   bits = NULL;
    HDC     mem;
    HBITMAP bmp, old;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = CERF_ICON_W;
    bi.bmiHeader.biHeight      = CERF_ICON_H;   /* positive => bottom-up */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    mem = CreateCompatibleDC(dc);
    if (!mem) return;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp && bits) {
        memcpy(bits, kCerfIconBgr, sizeof(kCerfIconBgr));
        old = (HBITMAP)SelectObject(mem, bmp);
        BitBlt(dc, x, y, CERF_ICON_W, CERF_ICON_H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
}

void PaintProgress(HWND hwnd, DumpState* st) {
    PAINTSTRUCT ps;
    HDC     dc = BeginPaint(hwnd, &ps);
    RECT    rc, fill;
    WCHAR   line[256];
    DWORD   pct = st->length ? (DWORD)(((__int64)st->bytes_done * 100) / st->length) : 0;
    int     y;
    int     bl, bt, br, bb, inner;
    HGDIOBJ oldbr;

    GetClientRect(hwnd, &rc);
    /* Clear with a real stock brush - CE FillRect does not honour the
       (COLOR_xxx + 1) system-color pseudo-brush, so the per-paint erase
       must use an HBRUSH; otherwise changing text overdraws itself. */
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(dc, TRANSPARENT);

    DrawCerfIcon(dc, rc.right - CERF_ICON_W - 8, 8);

    y = 172;
    if (!st->running && !st->finished) {
        DrawLineW(dc, 8, y, L"Ready! Pick a preset or set base/size, then GO.");
        EndPaint(hwnd, &ps);
        return;
    }

    wsprintfW(line, L"Reading PA 0x%08X   %u%%", st->cur_pa, (unsigned)pct);
    DrawLineW(dc, 8, y, line); y += 20;

    if (st->segmented) {
        DWORD total = st->seg_bytes
                      ? (st->length + st->seg_bytes - 1) / st->seg_bytes : 1;
        DWORD cur = st->segs_written + 1;
        if (cur > total) cur = total;
        wsprintfW(line, L"Segment %u/%u   %u / %u MB   0xFF pages: %u",
                  (unsigned)cur, (unsigned)total,
                  (unsigned)(st->bytes_done >> 20), (unsigned)(st->length >> 20),
                  (unsigned)st->fault_pages);
    } else {
        wsprintfW(line, L"Written: %u / %u MB    0xFF-filled pages: %u",
                  (unsigned)(st->bytes_done >> 20), (unsigned)(st->length >> 20),
                  (unsigned)st->fault_pages);
    }
    DrawLineW(dc, 8, y, line); y += 26;

    /* Progress bar: hollow Rectangle for the frame (CE has no FrameRect),
       FillRect for the filled portion. */
    bl = 8; br = rc.right - 8; bt = y; bb = y + 18;
    oldbr = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, bl, bt, br, bb);
    SelectObject(dc, oldbr);
    inner = br - bl - 2;
    if (inner < 0) inner = 0;
    fill.left = bl + 1; fill.top = bt + 1; fill.bottom = bb - 1;
    fill.right = fill.left + (LONG)(((__int64)inner * pct) / 100);
    FillRect(dc, &fill, (HBRUSH)GetStockObject(GRAY_BRUSH));

    EndPaint(hwnd, &ps);
}
