/* CERF DirectDraw probe: DirectDrawCreate + primary-surface create/lock/fill,
   to exercise a guest's DirectDraw HAL on demand. DirectDrawCreate is resolved
   at runtime (no ddraw import lib) so the one binary loads on CE4..CE7. */

#include <windows.h>
#include <ddraw.h>

#ifndef DDLOCK_WAITNOTBUSY      /* CE4.2/CE5 ddraw.h predates this flag */
#define DDLOCK_WAITNOTBUSY DDLOCK_WAIT
#endif

/* CE5.0 (Zune / devemu_ce5) DirectDraw: no-Compact vtable (ce6-oak headers) but the
   CLASSIC DDSURFACEDESC layout - ddsCaps at 0x68 (ce6=0x64), PRIMARYSURFACE=0x200
   (ce6=0x40). Field names match DDSURFACEDESC so the rest of the probe is identical. */
#ifdef CERF_CE5_DESC
#pragma pack(push, 4)
typedef struct {
    DWORD      dwSize;             /* 0x00 */
    DWORD      dwFlags;            /* 0x04 */
    DWORD      dwHeight;           /* 0x08 */
    DWORD      dwWidth;            /* 0x0C */
    LONG       lPitch;            /* 0x10 */
    DWORD      dwBackBufferCount; /* 0x14 */
    DWORD      dwRefreshRate;     /* 0x18 */
    DWORD      dwAlphaBitDepth;   /* 0x1C */
    DWORD      dwReserved;        /* 0x20 */
    LPVOID     lpSurface;         /* 0x24 */
    DDCOLORKEY ck0, ck1, ck2, ck3;/* 0x28..0x47 */
    DDPIXELFORMAT ddpfPixelFormat;/* 0x48 (0x20) */
    struct { DWORD dwCaps; } ddsCaps; /* 0x68 */
} CerfDdsd;                       /* 0x6C */
#pragma pack(pop)
#define DDSD_TYPE        CerfDdsd
#define CERF_CAP_PRIMARY  0x00000200u
#else
#define DDSD_TYPE        DDSURFACEDESC
#define CERF_CAP_PRIMARY  DDSCAPS_PRIMARYSURFACE
#endif

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
    DDSD_TYPE ddsd;
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

#ifdef CERF_CE5_DESC
    /* CE5.0 ddraw rejects DDSCL_FULLSCREEN alone (0x80070057); a primary surface needs
       exclusive mode. ce6-oak headers omit DDSCL_EXCLUSIVE; ce5-standard defines it 0x10. */
    hr = dd->lpVtbl->SetCooperativeLevel(dd, hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
#else
    hr = dd->lpVtbl->SetCooperativeLevel(dd, hwnd, DDSCL_FULLSCREEN);
#endif
    DbgHr(L"SetCooperativeLevel", hr);

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = CERF_CAP_PRIMARY;
    hr = dd->lpVtbl->CreateSurface(dd, (LPDDSURFACEDESC)&ddsd, &primary, NULL);
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
        hr = primary->lpVtbl->Lock(primary, NULL, (LPDDSURFACEDESC)&ddsd,
                                   DDLOCK_WAITNOTBUSY, NULL);
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
