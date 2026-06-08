/* CERF DirectDraw probe: DirectDrawCreate + primary-surface create/lock/fill,
   to exercise a guest's DirectDraw HAL on demand. DirectDrawCreate is resolved
   at runtime (no ddraw import lib) so the one binary loads on CE4..CE7. */

#include <windows.h>
#include <ddraw.h>

typedef HRESULT (WINAPI *PFN_DirectDrawCreate)(GUID*, LPDIRECTDRAW*, IUnknown*);

static void Dbg(const wchar_t* s) { OutputDebugStringW(s); }

static void DbgHr(const wchar_t* tag, HRESULT hr) {
    wchar_t b[128];
    wsprintfW(b, L"[ddrawtest] %s hr=0x%08X\r\n", tag, (unsigned)hr);
    OutputDebugStringW(b);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show) {
    WNDCLASSW wc;
    HWND hwnd;
    HMODULE hDD;
    PFN_DirectDrawCreate pDDC;
    LPDIRECTDRAW dd = NULL;
    LPDIRECTDRAWSURFACE primary = NULL;
    DDSURFACEDESC ddsd;
    HRESULT hr;
    int frame;
    wchar_t summary[256];

    (void)hPrev; (void)cmd; (void)show;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"CerfDDrawTest";
    RegisterClassW(&wc);
    hwnd = CreateWindowW(L"CerfDDrawTest", L"CERF DDraw Probe",
                         WS_VISIBLE | WS_POPUP, 0, 0,
                         GetSystemMetrics(SM_CXSCREEN),
                         GetSystemMetrics(SM_CYSCREEN),
                         NULL, NULL, hInst, NULL);

    Dbg(L"[ddrawtest] start\r\n");

    hDD = LoadLibraryW(L"ddraw.dll");
    if (!hDD) {
        DbgHr(L"LoadLibrary ddraw.dll FAILED gle", GetLastError());
        MessageBoxW(hwnd, L"ddraw.dll not present", L"DDraw Probe", MB_OK);
        return 1;
    }
    pDDC = (PFN_DirectDrawCreate)GetProcAddressW(hDD, L"DirectDrawCreate");
    if (!pDDC) {
        DbgHr(L"GetProcAddress DirectDrawCreate FAILED gle", GetLastError());
        MessageBoxW(hwnd, L"DirectDrawCreate missing", L"DDraw Probe", MB_OK);
        return 1;
    }

    /* This call is what drives the guest's DirectDraw runtime to bind the
       display driver and invoke its HALInit. */
    hr = pDDC(NULL, &dd, NULL);
    DbgHr(L"DirectDrawCreate", hr);
    if (FAILED(hr) || !dd) {
        wsprintfW(summary, L"DirectDrawCreate failed: 0x%08X", (unsigned)hr);
        MessageBoxW(hwnd, summary, L"DDraw Probe", MB_OK);
        return 1;
    }

    hr = dd->lpVtbl->SetCooperativeLevel(dd, hwnd, DDSCL_FULLSCREEN);
    DbgHr(L"SetCooperativeLevel", hr);

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = dd->lpVtbl->CreateSurface(dd, &ddsd, &primary, NULL);
    DbgHr(L"CreateSurface(primary)", hr);
    if (FAILED(hr) || !primary) {
        wsprintfW(summary, L"DirectDrawCreate OK, CreateSurface failed: 0x%08X",
                  (unsigned)hr);
        MessageBoxW(hwnd, summary, L"DDraw Probe", MB_OK);
        dd->lpVtbl->Release(dd);
        return 1;
    }

    /* Lock + fill the primary with a cycling colour for a few seconds. A
       working DirectDraw HAL paints the screen; a broken one fails Lock. */
    for (frame = 0; frame < 120; ++frame) {
        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        hr = primary->lpVtbl->Lock(primary, NULL, &ddsd, DDLOCK_WAITNOTBUSY, NULL);
        if (frame == 0) DbgHr(L"Lock(primary)#0", hr);
        if (SUCCEEDED(hr) && ddsd.lpSurface) {
            unsigned char* row = (unsigned char*)ddsd.lpSurface;
            unsigned short col = (unsigned short)((frame * 0x0821) & 0xFFFF);
            unsigned int y;
            for (y = 0; y < ddsd.dwHeight; ++y) {
                unsigned short* px = (unsigned short*)row;
                unsigned int x;
                for (x = 0; x < ddsd.dwWidth; ++x) px[x] = col;
                row += ddsd.lPitch;
            }
            primary->lpVtbl->Unlock(primary, NULL);
        } else if (frame == 0) {
            DbgHr(L"Lock#0 failed - stop draw loop", hr);
            break;
        }
        Sleep(50);
    }

    wsprintfW(summary,
              L"DDraw probe done.\nDirectDrawCreate + CreateSurface(primary) OK.\n"
              L"Locked primary %ux%u pitch=%d.\nSee OutputDebugString for HRESULTs.",
              (unsigned)ddsd.dwWidth, (unsigned)ddsd.dwHeight, (int)ddsd.lPitch);
    MessageBoxW(hwnd, summary, L"DDraw Probe", MB_OK);

    primary->lpVtbl->Release(primary);
    dd->lpVtbl->Release(dd);
    Dbg(L"[ddrawtest] done\r\n");
    return 0;
}
