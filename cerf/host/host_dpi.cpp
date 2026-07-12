#define NOMINMAX

#include "host_dpi.h"

#include "../core/cerf_emulator.h"

REGISTER_SERVICE(HostDpi);

void HostDpi::OnReady() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    get_dpi_for_window_ =
        (GetDpiForWindow_t)GetProcAddress(u32, "GetDpiForWindow");
    adjust_for_dpi_ =
        (AdjustWindowRectExForDpi_t)GetProcAddress(u32, "AdjustWindowRectExForDpi");
}

UINT HostDpi::ForWindow(HWND h) const {
    if (get_dpi_for_window_) return get_dpi_for_window_(h);
    HDC dc = GetDC(h);
    if (!dc) return USER_DEFAULT_SCREEN_DPI;
    const UINT dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(h, dc);
    return dpi;
}

BOOL HostDpi::AdjustForDpi(RECT& r, DWORD style, BOOL menu, DWORD ex,
                           UINT dpi) const {
    if (adjust_for_dpi_) return adjust_for_dpi_(&r, style, menu, ex, dpi);
    return AdjustWindowRectEx(&r, style, menu, ex);
}
