#include <windows.h>
/* INITGUID: this TU instantiates the DEFINE_GUIDs from ddrawi.h
   (GUID_GetDriverInfo_VidMemBase) that GetDriverInfo is matched against. */
#define INITGUID
#include <initguid.h>
#include <winddi.h>
#include <gpe.h>
#include <ddgpe.h>   /* brings in ddrawi.h (DDHALINFO, DDHAL_DD*CALLBACKS) */
#include "cerf_ddhal_wm.h"

extern ULONG g_EngineVersion;
extern "C" void CerfGetVideoMem(unsigned long* base, unsigned long* size,
                                unsigned long* freeBytes);

/* Generic DDGPE framework surface/device callbacks (ddgpe.lib, ddhsurf.cpp).
   cerf_guest wires its DDHALINFO straight at these — no DSS/overlay Hal*
   wrappers, since the CERF framebuffer is software. */
extern "C" DWORD WINAPI DDGPECreateSurface(LPDDHAL_CREATESURFACEDATA);
extern "C" DWORD WINAPI DDGPECanCreateSurface(LPDDHAL_CANCREATESURFACEDATA);
extern "C" DWORD WINAPI DDGPEDestroySurface(LPDDHAL_DESTROYSURFACEDATA);
extern "C" DWORD WINAPI DDGPEFlip(LPDDHAL_FLIPDATA);
extern "C" DWORD WINAPI DDGPELock(LPDDHAL_LOCKDATA);
extern "C" DWORD WINAPI DDGPEUnlock(LPDDHAL_UNLOCKDATA);
extern "C" DWORD WINAPI DDGPESetColorKey(LPDDHAL_SETCOLORKEYDATA);
extern "C" DWORD WINAPI DDGPEGetFlipStatus(LPDDHAL_GETFLIPSTATUSDATA);
extern "C" DWORD WINAPI DDGPESetPalette(LPDDHAL_SETPALETTEDATA);
extern "C" DWORD WINAPI DDGPEWaitForVerticalBlank(LPDDHAL_WAITFORVERTICALBLANKDATA);
extern "C" DWORD WINAPI DDGPECreatePalette(LPDDHAL_CREATEPALETTEDATA);

/* All cerf_guest blits complete synchronously on the host before the HAL call
   returns, so a surface is never mid-blt — both DDGBS_CANBLT and DDGBS_ISBLTDONE
   are satisfied. The GETBLTSTATUSDATA head (lpDD/lpDDSurface/dwFlags/ddRVal) is
   identical CE5/CE6, so this one handler serves every DDHALINFO generation. */
extern "C" DWORD WINAPI CerfGetBltStatus(LPDDHAL_GETBLTSTATUSDATA pd) {
    pd->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

extern "C" unsigned long CerfPrimaryShadowLockVa(unsigned long origin_pa);
extern "C" void  CerfPrimaryShadowPresent(void);
extern "C" ULONG CerfGpeFbMemBasePa(void);
extern "C" void* CerfMapFbGlobal(void);
extern "C" BOOL  CerfVidBackingIsGlobalFb(void);
extern "C" BOOL  CerfDDSurfFbInfo(void* lcl, ULONG* pa, int* stride, int* bpp,
                                  int* height);

/* An FB-PA surface has no standing cross-process VA, so the backing mode decides the
   Lock VA: GlobalFb returns the permanent global map; GuestRamHeap a shadow presented
   on Unlock. */
extern "C" DWORD WINAPI CerfDDGPELockWrap(LPDDHAL_LOCKDATA pd) {
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
    if (!va) { pd->ddRVal = DDERR_OUTOFMEMORY; return DDHAL_DRIVER_HANDLED; }
    pd->lpSurfData = (void*)(ULONG_PTR)va;
    pd->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

extern "C" DWORD WINAPI CerfDDGPEUnlockWrap(LPDDHAL_UNLOCKDATA pd) {
    ULONG pa; int stride = 0, bpp = 0, height = 0;
    if (CerfDDSurfFbInfo(pd->lpDDSurface, &pa, &stride, &bpp, &height)) {
        if (!CerfVidBackingIsGlobalFb())
            CerfPrimaryShadowPresent();  /* blit client-written shadow to FB-PA scanout */
        pd->ddRVal = DD_OK;
        return DDHAL_DRIVER_HANDLED;
    }
    return DDGPEUnlock(pd);
}

/* Diagnostic wrappers: log whether the kernel reaches the HAL for each surface
   create + what it answers, to settle why a system-memory (DDSD_LPSURFACE)
   surface gets E_NOTIMPL while a video surface succeeds. Delegate to the generic
   DDGPE* lib (the path stock HalCreateSurface uses for system memory). */
static DWORD WINAPI CerfCreateSurface(LPDDHAL_CREATESURFACEDATA pd) {
    DWORD caps  = pd->lpDDSurfaceDesc ? pd->lpDDSurfaceDesc->ddsCaps.dwCaps : 0;
    DWORD flags = pd->lpDDSurfaceDesc ? pd->lpDDSurfaceDesc->dwFlags : 0;
    CERF_LOG_X_DEV("cerf_guest: HAL CreateSurface caps", caps);
    CERF_LOG_X_DEV("cerf_guest: HAL CreateSurface dwFlags", flags);
    DWORD r = DDGPECreateSurface(pd);
    CERF_LOG_X_DEV("cerf_guest: HAL CreateSurface ddRVal", (DWORD)pd->ddRVal);
    CERF_LOG_X_DEV("cerf_guest: HAL CreateSurface result", r);
    return r;
}
static DWORD WINAPI CerfCanCreateSurface(LPDDHAL_CANCREATESURFACEDATA pd) {
    DWORD caps  = pd->lpDDSurfaceDesc ? pd->lpDDSurfaceDesc->ddsCaps.dwCaps : 0;
    CERF_LOG_X_DEV("cerf_guest: HAL CanCreateSurface caps", caps);
    DWORD r = DDGPECanCreateSurface(pd);
    CERF_LOG_X_DEV("cerf_guest: HAL CanCreateSurface ddRVal", (DWORD)pd->ddRVal);
    return r;
}

/* Field order + flags per ce6-oak ddrawi.h: DDHAL_DDCALLBACKS:713,
   DDHAL_DDSURFACECALLBACKS:747. */
static DDHAL_DDCALLBACKS g_cbDDCallbacks = {
    sizeof(DDHAL_DDCALLBACKS),
    DDHAL_CB32_CREATESURFACE | DDHAL_CB32_CANCREATESURFACE |
        DDHAL_CB32_WAITFORVERTICALBLANK | DDHAL_CB32_CREATEPALETTE,
    CerfCreateSurface,         /* CreateSurface */
    DDGPEWaitForVerticalBlank, /* WaitForVerticalBlank */
    CerfCanCreateSurface,      /* CanCreateSurface */
    DDGPECreatePalette,        /* CreatePalette */
    NULL                       /* GetScanLine */
};

static DDHAL_DDSURFACECALLBACKS g_cbDDSurfaceCallbacks = {
    sizeof(DDHAL_DDSURFACECALLBACKS),
    DDHAL_SURFCB32_DESTROYSURFACE | DDHAL_SURFCB32_FLIP | DDHAL_SURFCB32_LOCK |
        DDHAL_SURFCB32_UNLOCK | DDHAL_SURFCB32_SETCOLORKEY |
        DDHAL_SURFCB32_GETFLIPSTATUS | DDHAL_SURFCB32_SETPALETTE |
        DDHAL_SURFCB32_GETBLTSTATUS,
    DDGPEDestroySurface,       /* DestroySurface */
    DDGPEFlip,                 /* Flip */
    CerfDDGPELockWrap,         /* Lock */
    CerfDDGPEUnlockWrap,       /* Unlock */
    DDGPESetColorKey,          /* SetColorKey */
    CerfGetBltStatus,          /* GetBltStatus */
    DDGPEGetFlipStatus,        /* GetFlipStatus */
    NULL,                      /* UpdateOverlay */
    NULL,                      /* SetOverlayPosition */
    DDGPESetPalette            /* SetPalette */
};

/* Answers the DirectDraw VidMemBase query with the offscreen base the driver
   advertises (mirrors omap halcaps.cpp HalGetDriverInfo, VidMemBase arm). */
extern "C" DWORD WINAPI CerfHalGetDriverInfo(LPDDHAL_GETDRIVERINFODATA lpInput) {
    lpInput->ddRVal = DDERR_CURRENTLYNOTAVAIL;
    if (IsEqualIID(lpInput->guidInfo, GUID_GetDriverInfo_VidMemBase)) {
        unsigned long base = 0, size = 0, freeBytes = 0;
        CerfGetVideoMem(&base, &size, &freeBytes);
        *(DWORD*)(lpInput->lpvData) = base;
        lpInput->dwActualSize = sizeof(DWORD);
        lpInput->ddRVal = DD_OK;
        CERF_LOG_X_DEV("cerf_guest: HalGetDriverInfo VidMemBase", base);
    }
    return DDHAL_DRIVER_HANDLED;
}

/* Driver-provided; the lib's HALInit (ddhinit.cpp:102) calls this. Mirrors omap
   halcaps.cpp buildDDHALInfo, minus DSS/overlay: advertise the real offscreen
   video-memory region the driver carves surfaces from. */
EXTERN_C void buildDDHALInfo(LPDDHALINFO lpddhi, DWORD modeidx) {
    unsigned long vidBase = 0, vidSize = 0, vidFree = 0;
    CerfGetVideoMem(&vidBase, &vidSize, &vidFree);

    memset(lpddhi, 0, sizeof(DDHALINFO));
    lpddhi->dwSize               = sizeof(DDHALINFO);
    lpddhi->lpDDCallbacks        = &g_cbDDCallbacks;
    lpddhi->lpDDSurfaceCallbacks = &g_cbDDSurfaceCallbacks;
    lpddhi->GetDriverInfo        = CerfHalGetDriverInfo;

    lpddhi->ddCaps.dwSize        = sizeof(DDCAPS);
    lpddhi->ddCaps.dwVidMemTotal = vidSize;
    lpddhi->ddCaps.dwVidMemFree  = vidFree;
    lpddhi->ddCaps.dwVidMemStride = 0;
    lpddhi->ddCaps.ddsCaps.dwCaps =
        DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER |
        DDSCAPS_FLIP | DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY;
    lpddhi->ddCaps.dwNumFourCCCodes = 0;
    lpddhi->ddCaps.dwPalCaps     = 0;
    lpddhi->ddCaps.dwBltCaps     = DDBLTCAPS_READSYSMEM | DDBLTCAPS_WRITESYSMEM;
    SETROPBIT(lpddhi->ddCaps.dwRops, SRCCOPY);
    SETROPBIT(lpddhi->ddCaps.dwRops, PATCOPY);
    SETROPBIT(lpddhi->ddCaps.dwRops, BLACKNESS);
    SETROPBIT(lpddhi->ddCaps.dwRops, WHITENESS);
    lpddhi->ddCaps.dwCKeyCaps    = DDCKEYCAPS_SRCBLT;
    lpddhi->ddCaps.dwAlphaCaps   = DDALPHACAPS_ALPHAPIXELS | DDALPHACAPS_ALPHACONSTANT;
    lpddhi->ddCaps.dwMiscCaps    = 0;
}

/* Software HEL caps (system memory only), copy of the lib's static
   FillHelCaps (ddhinit.cpp:15) which cerf_guest can't link (it's static). */
static void CerfFillHelCaps(DDCAPS* pDDCaps) {
    memset(pDDCaps, 0, sizeof(DDCAPS));
    pDDCaps->dwSize = sizeof(DDCAPS);
    pDDCaps->ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_SYSTEMMEMORY;
    pDDCaps->dwBltCaps   = DDBLTCAPS_READSYSMEM | DDBLTCAPS_WRITESYSMEM;
    pDDCaps->dwCKeyCaps  = DDCKEYCAPS_SRCBLT;
    pDDCaps->dwAlphaCaps = DDALPHACAPS_ALPHAPIXELS | DDALPHACAPS_ALPHACONSTANT |
                           DDALPHACAPS_NONPREMULT;
    SETROPBIT(pDDCaps->dwRops, SRCCOPY);
    SETROPBIT(pDDCaps->dwRops, PATCOPY);
    SETROPBIT(pDDCaps->dwRops, BLACKNESS);
    SETROPBIT(pDDCaps->dwRops, WHITENESS);
}

/* HEL caps filled inline at the WM 112-byte DDCAPS size — CerfFillHelCaps writes a
   128-byte CE6 DDCAPS and would overrun ddHelCaps into lpdwFourCC. */
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
        DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER |
        DDSCAPS_FLIP | DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY;
    h->ddCaps.dwBltCaps   = DDBLTCAPS_READSYSMEM | DDBLTCAPS_WRITESYSMEM;
    h->ddCaps.dwCKeyCaps  = DDCKEYCAPS_SRCBLT;
    h->ddCaps.dwAlphaCaps = DDALPHACAPS_ALPHAPIXELS | DDALPHACAPS_ALPHACONSTANT;
    SETROPBIT(h->ddCaps.dwRops, SRCCOPY);
    SETROPBIT(h->ddCaps.dwRops, PATCOPY);
    SETROPBIT(h->ddCaps.dwRops, BLACKNESS);
    SETROPBIT(h->ddCaps.dwRops, WHITENESS);

    h->ddHelCaps.dwSize       = sizeof(Wm_DDCAPS);
    h->ddHelCaps.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_SYSTEMMEMORY;
    h->ddHelCaps.dwBltCaps    = DDBLTCAPS_READSYSMEM | DDBLTCAPS_WRITESYSMEM;
    h->ddHelCaps.dwCKeyCaps   = DDCKEYCAPS_SRCBLT;
    SETROPBIT(h->ddHelCaps.dwRops, SRCCOPY);
    SETROPBIT(h->ddHelCaps.dwRops, PATCOPY);
    SETROPBIT(h->ddHelCaps.dwRops, BLACKNESS);
    SETROPBIT(h->ddHelCaps.dwRops, WHITENESS);

    return TRUE;
}

extern "C" BOOL Ce5HALInit(void* lpddhi);

/* Discriminator is GetVersionEx (measured): 5.0=CE5/460, 5.1/5.2=WM/252,
   >=6=CE6-CE7/284. NOT g_EngineVersion — WinCE5, Zune and WM5 all report engine
   0x40001 yet split 460 vs 252, so keying on it picks the wrong DDHALINFO size. */
EXTERN_C BOOL WINAPI HALInit(LPDDHALINFO lpddhi, BOOL unused1, DWORD modeidx) {
    OSVERSIONINFOW ovi;
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    GetVersionExW(&ovi);

    if (ovi.dwMajorVersion >= 6) {
        buildDDHALInfo(lpddhi, modeidx);
        CerfFillHelCaps(&lpddhi->ddHelCaps);
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
