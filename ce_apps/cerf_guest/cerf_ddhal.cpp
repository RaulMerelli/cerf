#include <windows.h>
#include "cerf_ddgpe.h"
#include "include/ddraw_ce6.h"
#include "include/ddraw_wm.h"

extern "C" void CerfGetVideoMem(unsigned long* base, unsigned long* size,
                                unsigned long* freeBytes);

extern "C" DWORD WINAPI DDGPECreateSurface(Ce6_DDHAL_CREATESURFACEDATA*);
extern "C" DWORD WINAPI DDGPECanCreateSurface(Ce6_DDHAL_CANCREATESURFACEDATA*);
extern "C" DWORD WINAPI DDGPEDestroySurface(Ce6_DDHAL_DESTROYSURFACEDATA*);
extern "C" DWORD WINAPI DDGPEFlip(Ce6_DDHAL_FLIPDATA*);
extern "C" DWORD WINAPI DDGPELock(Ce6_DDHAL_LOCKDATA*);
extern "C" DWORD WINAPI DDGPEUnlock(Ce6_DDHAL_UNLOCKDATA*);
extern "C" DWORD WINAPI DDGPESetColorKey(Ce6_DDHAL_SETCOLORKEYDATA*);
extern "C" DWORD WINAPI DDGPEGetFlipStatus(Ce6_DDHAL_GETFLIPSTATUSDATA*);
extern "C" DWORD WINAPI DDGPESetPalette(Ce6_DDHAL_SETPALETTEDATA*);
extern "C" DWORD WINAPI DDGPEWaitForVerticalBlank(Ce6_DDHAL_WAITFORVERTICALBLANKDATA*);
extern "C" DWORD WINAPI DDGPECreatePalette(Ce6_DDHAL_CREATEPALETTEDATA*);

extern "C" DWORD WINAPI CerfGetBltStatus(Ce6_DDHAL_GETBLTSTATUSDATA* pd) {
    pd->ddRVal = CERF_DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

extern "C" unsigned long CerfPrimaryShadowLockVa(unsigned long origin_pa);
extern "C" void  CerfPrimaryShadowPresent(void);
extern "C" ULONG CerfGpeFbMemBasePa(void);
extern "C" void* CerfMapFbGlobal(void);
extern "C" BOOL  CerfVidBackingIsGlobalFb(void);
extern "C" BOOL  CerfDDSurfFbInfo(void* lcl, ULONG* pa, int* stride, int* bpp,
                                  int* height);

extern "C" DWORD WINAPI CerfDDGPELockWrap(Ce6_DDHAL_LOCKDATA* pd) {
    ULONG pa; int stride = 0, bpp = 0, height = 0;
    if (!CerfDDSurfFbInfo(pd->lpDDSurface, &pa, &stride, &bpp, &height))
        return DDGPELock(pd);
    int x = pd->bHasRect ? pd->rArea.left : 0;
    int y = pd->bHasRect ? pd->rArea.top  : 0;
    ULONG origin = pa + (ULONG)y * (ULONG)stride + (ULONG)x * ((ULONG)bpp / 8u);
    unsigned long va;
    if (CerfVidBackingIsGlobalFb()) {
        void* g = CerfMapFbGlobal();
        va = g ? (unsigned long)(ULONG_PTR)g + (origin - CerfGpeFbMemBasePa()) : 0;
    } else {
        va = CerfPrimaryShadowLockVa(origin);
    }
    if (!va) { pd->ddRVal = CERF_DDERR_OUTOFMEMORY; return DDHAL_DRIVER_HANDLED; }
    pd->lpSurfData = (void*)(ULONG_PTR)va;
    pd->ddRVal = CERF_DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

extern "C" DWORD WINAPI CerfDDGPEUnlockWrap(Ce6_DDHAL_UNLOCKDATA* pd) {
    ULONG pa; int stride = 0, bpp = 0, height = 0;
    if (CerfDDSurfFbInfo(pd->lpDDSurface, &pa, &stride, &bpp, &height)) {
        if (!CerfVidBackingIsGlobalFb())
            CerfPrimaryShadowPresent();
        pd->ddRVal = CERF_DD_OK;
        return DDHAL_DRIVER_HANDLED;
    }
    return DDGPEUnlock(pd);
}

static DWORD WINAPI CerfCreateSurface(Ce6_DDHAL_CREATESURFACEDATA* pd) {
    DWORD caps  = pd->lpDDSurfaceDesc ? pd->lpDDSurfaceDesc->ddsCaps.dwCaps : 0;
    DWORD flags = pd->lpDDSurfaceDesc ? pd->lpDDSurfaceDesc->dwFlags : 0;
    CERF_LOG_X_DEV("cerf_guest: HAL CreateSurface caps", caps);
    CERF_LOG_X_DEV("cerf_guest: HAL CreateSurface dwFlags", flags);
    DWORD r = DDGPECreateSurface(pd);
    CERF_LOG_X_DEV("cerf_guest: HAL CreateSurface ddRVal", (DWORD)pd->ddRVal);
    CERF_LOG_X_DEV("cerf_guest: HAL CreateSurface result", r);
    return r;
}
static DWORD WINAPI CerfCanCreateSurface(Ce6_DDHAL_CANCREATESURFACEDATA* pd) {
    DWORD caps = pd->lpDDSurfaceDesc ? pd->lpDDSurfaceDesc->ddsCaps.dwCaps : 0;
    CERF_LOG_X_DEV("cerf_guest: HAL CanCreateSurface caps", caps);
    DWORD r = DDGPECanCreateSurface(pd);
    CERF_LOG_X_DEV("cerf_guest: HAL CanCreateSurface ddRVal", (DWORD)pd->ddRVal);
    return r;
}

static Ce6_DDHAL_DDCALLBACKS g_cbDDCallbacks = {
    sizeof(Ce6_DDHAL_DDCALLBACKS),
    CE6_DDHAL_CB32_CREATESURFACE | CE6_DDHAL_CB32_CANCREATESURFACE |
        CE6_DDHAL_CB32_WAITFORVERTICALBLANK | CE6_DDHAL_CB32_CREATEPALETTE,
    (PVOID)CerfCreateSurface,
    (PVOID)DDGPEWaitForVerticalBlank,
    (PVOID)CerfCanCreateSurface,
    (PVOID)DDGPECreatePalette,
    NULL
};

static Ce6_DDHAL_DDSURFACECALLBACKS g_cbDDSurfaceCallbacks = {
    sizeof(Ce6_DDHAL_DDSURFACECALLBACKS),
    CE6_DDHAL_SURFCB32_DESTROYSURFACE | CE6_DDHAL_SURFCB32_FLIP |
        CE6_DDHAL_SURFCB32_LOCK | CE6_DDHAL_SURFCB32_UNLOCK |
        CE6_DDHAL_SURFCB32_SETCOLORKEY | CE6_DDHAL_SURFCB32_GETFLIPSTATUS |
        CE6_DDHAL_SURFCB32_SETPALETTE | CE6_DDHAL_SURFCB32_GETBLTSTATUS,
    (PVOID)DDGPEDestroySurface,
    (PVOID)DDGPEFlip,
    (PVOID)CerfDDGPELockWrap,
    (PVOID)CerfDDGPEUnlockWrap,
    (PVOID)DDGPESetColorKey,
    (PVOID)CerfGetBltStatus,
    (PVOID)DDGPEGetFlipStatus,
    NULL,
    NULL,
    (PVOID)DDGPESetPalette
};

static const GUID kCerfGuidVidMemBase =
    { 0x74a05a72, 0xf182, 0x43f7, { 0xad, 0xaa, 0x67, 0x30, 0xed, 0xca, 0x00, 0xf6 } };

static bool CerfGuidEq(const GUID& a, const GUID& b) {
    if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3) return false;
    for (int i = 0; i < 8; ++i) if (a.Data4[i] != b.Data4[i]) return false;
    return true;
}

extern "C" DWORD WINAPI CerfHalGetDriverInfo(Ce6_DDHAL_GETDRIVERINFODATA* lpInput) {
    lpInput->ddRVal = CERF_DDERR_CURRENTLYNOTAVAIL;
    if (CerfGuidEq(lpInput->guidInfo, kCerfGuidVidMemBase)) {
        unsigned long base = 0, size = 0, freeBytes = 0;
        CerfGetVideoMem(&base, &size, &freeBytes);
        *(DWORD*)(lpInput->lpvData) = base;
        lpInput->dwActualSize = sizeof(DWORD);
        lpInput->ddRVal = CERF_DD_OK;
        CERF_LOG_X_DEV("cerf_guest: HalGetDriverInfo VidMemBase", base);
    }
    return DDHAL_DRIVER_HANDLED;
}

EXTERN_C void buildDDHALInfo(Ce6_DDHALINFO* h, DWORD modeidx) {
    unsigned long vidBase = 0, vidSize = 0, vidFree = 0;
    CerfGetVideoMem(&vidBase, &vidSize, &vidFree);

    memset(h, 0, sizeof(Ce6_DDHALINFO));
    h->dwSize               = sizeof(Ce6_DDHALINFO);
    h->lpDDCallbacks        = &g_cbDDCallbacks;
    h->lpDDSurfaceCallbacks = &g_cbDDSurfaceCallbacks;
    h->GetDriverInfo        = (PVOID)CerfHalGetDriverInfo;

    h->ddCaps.dwSize        = sizeof(Ce6_DDCAPS);
    h->ddCaps.dwVidMemTotal = vidSize;
    h->ddCaps.dwVidMemFree  = vidFree;
    h->ddCaps.dwVidMemStride = 0;
    h->ddCaps.ddsCaps.dwCaps =
        CE6_DDSCAPS_PRIMARYSURFACE | CE6_DDSCAPS_FRONTBUFFER | CE6_DDSCAPS_BACKBUFFER |
        CE6_DDSCAPS_FLIP | CE6_DDSCAPS_SYSTEMMEMORY | CE6_DDSCAPS_VIDEOMEMORY;
    h->ddCaps.dwNumFourCCCodes = 0;
    h->ddCaps.dwPalCaps     = 0;
    h->ddCaps.dwBltCaps     = CERF_DDBLTCAPS_READSYSMEM | CERF_DDBLTCAPS_WRITESYSMEM;
    CERF_SETROPBIT(h->ddCaps.dwRops, SRCCOPY);
    CERF_SETROPBIT(h->ddCaps.dwRops, PATCOPY);
    CERF_SETROPBIT(h->ddCaps.dwRops, BLACKNESS);
    CERF_SETROPBIT(h->ddCaps.dwRops, WHITENESS);
    h->ddCaps.dwCKeyCaps    = CERF_DDCKEYCAPS_SRCBLT;
    h->ddCaps.dwAlphaCaps   = CERF_DDALPHACAPS_ALPHAPIXELS | CERF_DDALPHACAPS_ALPHACONSTANT;
    h->ddCaps.dwMiscCaps    = 0;
}

static void CerfFillHelCaps(Ce6_DDCAPS* pDDCaps) {
    memset(pDDCaps, 0, sizeof(Ce6_DDCAPS));
    pDDCaps->dwSize = sizeof(Ce6_DDCAPS);
    pDDCaps->ddsCaps.dwCaps = CE6_DDSCAPS_PRIMARYSURFACE | CE6_DDSCAPS_SYSTEMMEMORY;
    pDDCaps->dwBltCaps   = CERF_DDBLTCAPS_READSYSMEM | CERF_DDBLTCAPS_WRITESYSMEM;
    pDDCaps->dwCKeyCaps  = CERF_DDCKEYCAPS_SRCBLT;
    pDDCaps->dwAlphaCaps = CERF_DDALPHACAPS_ALPHAPIXELS | CERF_DDALPHACAPS_ALPHACONSTANT |
                           CERF_DDALPHACAPS_NONPREMULT;
    CERF_SETROPBIT(pDDCaps->dwRops, SRCCOPY);
    CERF_SETROPBIT(pDDCaps->dwRops, PATCOPY);
    CERF_SETROPBIT(pDDCaps->dwRops, BLACKNESS);
    CERF_SETROPBIT(pDDCaps->dwRops, WHITENESS);
}

extern "C" BOOL WmHALInit(void* lpddhi) {
    unsigned long vidBase = 0, vidSize = 0, vidFree = 0;
    CerfGetVideoMem(&vidBase, &vidSize, &vidFree);

    Wm_DDHALINFO* h = (Wm_DDHALINFO*)lpddhi;
    memset(h, 0, sizeof(Wm_DDHALINFO));
    h->dwSize               = sizeof(Wm_DDHALINFO);
    h->lpDDCallbacks        = &g_cbDDCallbacks;
    h->lpDDSurfaceCallbacks = &g_cbDDSurfaceCallbacks;
    h->lpDDPaletteCallbacks = NULL;
    h->GetDriverInfo        = (PVOID)CerfHalGetDriverInfo;

    h->ddCaps.dwSize        = sizeof(Wm_DDCAPS);
    h->ddCaps.dwVidMemTotal = vidSize;
    h->ddCaps.dwVidMemFree  = vidFree;
    h->ddCaps.ddsCaps.dwCaps =
        CE6_DDSCAPS_PRIMARYSURFACE | CE6_DDSCAPS_FRONTBUFFER | CE6_DDSCAPS_BACKBUFFER |
        CE6_DDSCAPS_FLIP | CE6_DDSCAPS_SYSTEMMEMORY | CE6_DDSCAPS_VIDEOMEMORY;
    h->ddCaps.dwBltCaps   = CERF_DDBLTCAPS_READSYSMEM | CERF_DDBLTCAPS_WRITESYSMEM;
    h->ddCaps.dwCKeyCaps  = CERF_DDCKEYCAPS_SRCBLT;
    h->ddCaps.dwAlphaCaps = CERF_DDALPHACAPS_ALPHAPIXELS | CERF_DDALPHACAPS_ALPHACONSTANT;
    CERF_SETROPBIT(h->ddCaps.dwRops, SRCCOPY);
    CERF_SETROPBIT(h->ddCaps.dwRops, PATCOPY);
    CERF_SETROPBIT(h->ddCaps.dwRops, BLACKNESS);
    CERF_SETROPBIT(h->ddCaps.dwRops, WHITENESS);

    h->ddHelCaps.dwSize       = sizeof(Wm_DDCAPS);
    h->ddHelCaps.ddsCaps.dwCaps = CE6_DDSCAPS_PRIMARYSURFACE | CE6_DDSCAPS_SYSTEMMEMORY;
    h->ddHelCaps.dwBltCaps    = CERF_DDBLTCAPS_READSYSMEM | CERF_DDBLTCAPS_WRITESYSMEM;
    h->ddHelCaps.dwCKeyCaps   = CERF_DDCKEYCAPS_SRCBLT;
    CERF_SETROPBIT(h->ddHelCaps.dwRops, SRCCOPY);
    CERF_SETROPBIT(h->ddHelCaps.dwRops, PATCOPY);
    CERF_SETROPBIT(h->ddHelCaps.dwRops, BLACKNESS);
    CERF_SETROPBIT(h->ddHelCaps.dwRops, WHITENESS);

    return TRUE;
}

extern "C" BOOL Ce5HALInit(void* lpddhi);

EXTERN_C BOOL WINAPI HALInit(void* lpddhi, BOOL unused1, DWORD modeidx) {
    OSVERSIONINFOW ovi;
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    GetVersionExW(&ovi);

    if (ovi.dwMajorVersion >= 6) {
        buildDDHALInfo((Ce6_DDHALINFO*)lpddhi, modeidx);
        CerfFillHelCaps(&((Ce6_DDHALINFO*)lpddhi)->ddHelCaps);
        CERF_LOG("cerf_guest: HALInit CE6/CE7 path done");
        return TRUE;
    }
    if (ovi.dwMajorVersion == 5 && ovi.dwMinorVersion >= 1) {
        BOOL ok = WmHALInit(lpddhi);
        CERF_LOG_X("cerf_guest: HALInit WM path ok", ok);
        return ok;
    }
    BOOL ok = Ce5HALInit(lpddhi);
    CERF_LOG_X("cerf_guest: HALInit CE5 path ok", ok);
    return ok;
}
