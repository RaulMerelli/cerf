#include <windows.h>
#include <winddi.h>
#include <gpe.h>
#include <ddgpe.h>   /* CE6 ddrawi.h: DDHAL_* DATA structs the DDGPE lib consumes */

#include "cerf_ddhal_ce5.h"
#include "cerf_debug_log.h"

/* CE5-generation DirectDraw HAL: fills the CE5 DDHALINFO layout and reshapes the
   two per-call DATA structs that differ from CE6 (CreateSurface, Lock) into the
   CE6 shapes the linked DDGPE lib reads, then delegates to the same DDGPE video
   callbacks the CE6 path uses. HW path only — no software HEL fallback. */

extern ULONG g_FbWidth;
extern ULONG g_FbHeight;
extern ULONG g_FbBpp;
extern ULONG g_FbStride;

extern "C" void  CerfGetVideoMem(unsigned long* base, unsigned long* size,
                                 unsigned long* freeBytes);
extern "C" ULONG CerfGpeFbMemBasePa(void);

/* Only CreateSurface and Lock need reshaping; every other callback's DATA head
   matches CE6, so the CE5 tables point straight at these DDGPE / FB-PA-lock
   wrappers. */
extern "C" DWORD WINAPI DDGPECreateSurface(LPDDHAL_CREATESURFACEDATA);
extern "C" DWORD WINAPI DDGPECanCreateSurface(LPDDHAL_CANCREATESURFACEDATA);
extern "C" DWORD WINAPI DDGPEDestroySurface(LPDDHAL_DESTROYSURFACEDATA);
extern "C" DWORD WINAPI DDGPEFlip(LPDDHAL_FLIPDATA);
extern "C" DWORD WINAPI DDGPESetColorKey(LPDDHAL_SETCOLORKEYDATA);
extern "C" DWORD WINAPI DDGPEGetFlipStatus(LPDDHAL_GETFLIPSTATUSDATA);
extern "C" DWORD WINAPI DDGPESetPalette(LPDDHAL_SETPALETTEDATA);
extern "C" DWORD WINAPI CerfDDGPELockWrap(LPDDHAL_LOCKDATA);
extern "C" DWORD WINAPI CerfDDGPEUnlockWrap(LPDDHAL_UNLOCKDATA);
extern "C" DWORD WINAPI CerfHalGetDriverInfo(LPDDHAL_GETDRIVERINFODATA);
extern "C" DWORD WINAPI DDGPECreatePalette(LPDDHAL_CREATEPALETTEDATA);
extern "C" DWORD WINAPI DDGPEWaitForVerticalBlank(LPDDHAL_WAITFORVERTICALBLANKDATA);
extern "C" DWORD WINAPI CerfGetBltStatus(LPDDHAL_GETBLTSTATUSDATA);
extern "C" unsigned long CerfDDGPESurfBufferVa(unsigned long surf);

/* The CE5 DDRAWI_DDRAWSURFACE_LCL differs from the ce6-oak LCL the DDGPE lib compiles
   against: DDGPE binds its DDGPESurf via lcl->dwReserved1 at ce6 byte 0x10, which in the
   CE5 LCL is lpAttachListFrom (a CE5-runtime field). Keep the binding in a side-map keyed
   by the LCL pointer; swap it into lcl+0x10 only during each DDGPE call, restore after. */
#define CE5_LCL_DWRESERVED1_OFF 0x10u   /* ce6-oak LCL dwReserved1 byte offset */
struct Ce5SurfBind { void* lcl; ULONG_PTR surf; };
static Ce5SurfBind s_ce5Binds[64];

static ULONG_PTR* Ce5BindFind(void* lcl) {
    for (int i = 0; i < 64; ++i) if (s_ce5Binds[i].lcl == lcl) return &s_ce5Binds[i].surf;
    return NULL;
}
/* Release the side-map slot when its surface is destroyed, so the 64-entry table
   does not saturate across the surface churn and start dropping new bindings. */
static void Ce5BindForget(void* lcl) {
    for (int i = 0; i < 64; ++i)
        if (s_ce5Binds[i].lcl == lcl) { s_ce5Binds[i].lcl = NULL; s_ce5Binds[i].surf = 0; return; }
}

/* DDSCAPS bits are renumbered CE5 (classic DirectX) vs the ce6-oak ddraw.h the
   DDGPE lib reads; remap by name or DDGPECreateSurface mis-reads PRIMARYSURFACE
   and never detects the primary. No-ce6-counterpart bits (COMPLEX) drop. */
static DWORD Ce5CapsToC6(DWORD ce5) {
    DWORD c6 = 0;
    if (ce5 & 0x00000002u) c6 |= 0x00000001u;  /* ALPHA */
    if (ce5 & 0x00000004u) c6 |= 0x00000002u;  /* BACKBUFFER */
    if (ce5 & 0x00000010u) c6 |= 0x00000004u;  /* FLIP */
    if (ce5 & 0x00000020u) c6 |= 0x00000008u;  /* FRONTBUFFER */
    if (ce5 & 0x00000080u) c6 |= 0x00000010u;  /* OVERLAY */
    if (ce5 & 0x00000200u) c6 |= 0x00000040u;  /* PRIMARYSURFACE */
    if (ce5 & 0x00000800u) c6 |= 0x00000080u;  /* SYSTEMMEMORY */
    if (ce5 & 0x00004000u) c6 |= 0x00000100u;  /* VIDEOMEMORY */
    if (ce5 & 0x00040000u) c6 |= 0x00008000u;  /* OWNDC */
    return c6;
}
/* DDGPECreateSurface decides surface placement and ORs in the ce6 VIDEOMEMORY/
   SYSTEMMEMORY bit; fold that decision back onto the original CE5 caps so the
   CE5-only bits gemstone set (COMPLEX/etc.) survive the round-trip. */
static DWORD Ce5CapsAddPlacementFromC6(DWORD ce5orig, DWORD c6) {
    if (c6 & 0x00000100u) ce5orig |= 0x00004000u;  /* VIDEOMEMORY */
    if (c6 & 0x00000080u) ce5orig |= 0x00000800u;  /* SYSTEMMEMORY */
    return ce5orig;
}

/* The CE5 per-surface LCL keeps ddsCaps at 0x20 and the DDGPESurf binding slot
   (ce6 dwReserved1) at 0x10, but the ce6 LCL the DDGPE lib reads has ddsCaps at
   0x8 and dwReserved1 at 0x10; marshal both around a DDGPECreateSurface call so
   DDGPE sees ce6-shaped caps + binding, then restore the CE5 fields. */
#define CE5_LCL_DDSCAPS_OFF 0x20u
#define CE6_LCL_DDSCAPS_OFF 0x08u
struct Ce5LclSave { ULONG_PTR off8; DWORD ce5caps; };

static void Ce5CreateMarshalIn(void* lcl, Ce5LclSave* s) {
    BYTE* b = (BYTE*)lcl;
    s->off8    = *(ULONG_PTR*)(b + CE6_LCL_DDSCAPS_OFF);
    s->ce5caps = *(DWORD*)(b + CE5_LCL_DDSCAPS_OFF);
    *(DWORD*)(b + CE6_LCL_DDSCAPS_OFF) = Ce5CapsToC6(s->ce5caps);
}
/* Pairs with Ce5LclLeave (which persists the binding + restores lcl+0x10); this
   folds DDGPE's placement decision into the CE5 caps and restores lcl+0x8. */
static void Ce5CreateMarshalOut(void* lcl, const Ce5LclSave* s) {
    BYTE* b = (BYTE*)lcl;
    DWORD c6caps = *(DWORD*)(b + CE6_LCL_DDSCAPS_OFF);
    *(DWORD*)(b + CE5_LCL_DDSCAPS_OFF) = Ce5CapsAddPlacementFromC6(s->ce5caps, c6caps);
    *(ULONG_PTR*)(b + CE6_LCL_DDSCAPS_OFF) = s->off8;
}
/* Before a DDGPE call: place the saved DDGPESurf into lcl+0x10 so DDGPE's
   GetDDGPESurf finds it; return the CE5 field value to restore afterward. */
static ULONG_PTR Ce5LclEnter(void* lcl) {
    if (!lcl) return 0;
    ULONG_PTR* slot = (ULONG_PTR*)((BYTE*)lcl + CE5_LCL_DWRESERVED1_OFF);
    ULONG_PTR ce5val = *slot;
    ULONG_PTR* b = Ce5BindFind(lcl);
    *slot = b ? *b : 0;
    return ce5val;
}
/* After a DDGPE call: persist whatever DDGPESurf DDGPE left in lcl+0x10 into the
   side-map; restore the CE5 lpAttachListFrom field. */
static void Ce5LclLeave(void* lcl, ULONG_PTR ce5val) {
    if (!lcl) return;
    ULONG_PTR* slot = (ULONG_PTR*)((BYTE*)lcl + CE5_LCL_DWRESERVED1_OFF);
    ULONG_PTR surf = *slot;
    ULONG_PTR* b = Ce5BindFind(lcl);
    if (!b) for (int i = 0; i < 64; ++i)
        if (!s_ce5Binds[i].lcl) { s_ce5Binds[i].lcl = lcl; b = &s_ce5Binds[i].surf; break; }
    if (b) *b = surf;
    *slot = ce5val;
}

/* CE5 _DDSURFACEDESC (ce5-standard ddraw.h): from 0x10 it diverges from the ce6-oak
   DDSURFACEDESC this driver compiles against (lPitch union + dwAlphaBitDepth shift
   ddsCaps to 0x68 vs CE6's 0x64), so passing it un-reshaped to the CE6 DDGPE
   functions reads ddsCaps/pixelformat wrong and builds the wrong surface. */
typedef struct _Ce5_DDSURFACEDESC {
    DWORD         dwSize;
    DWORD         dwFlags;
    DWORD         dwHeight;
    DWORD         dwWidth;
    union { LONG lPitch; DWORD dwLinearSize; } u1;
    DWORD         dwBackBufferCount;
    union { DWORD dwMipMapCount; DWORD dwZBufferBitDepth; DWORD dwRefreshRate; } u2;
    DWORD         dwAlphaBitDepth;
    DWORD         dwReserved;
    LPVOID        lpSurface;
    DDCOLORKEY    ddckCKDestOverlay;
    DDCOLORKEY    ddckCKDestBlt;
    DDCOLORKEY    ddckCKSrcOverlay;
    DDCOLORKEY    ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS       ddsCaps;
} Ce5_DDSURFACEDESC;

static void Ce5DescToC6(const Ce5_DDSURFACEDESC* a, DDSURFACEDESC* c6) {
    memset(c6, 0, sizeof(*c6));
    c6->dwSize            = sizeof(DDSURFACEDESC);
    c6->dwFlags           = a->dwFlags;
    c6->dwHeight          = a->dwHeight;
    c6->dwWidth           = a->dwWidth;
    c6->lPitch            = a->u1.lPitch;
    c6->dwBackBufferCount = a->dwBackBufferCount;
    c6->dwRefreshRate     = a->u2.dwRefreshRate;
    c6->lpSurface         = a->lpSurface;
    c6->ddckCKDestOverlay = a->ddckCKDestOverlay;
    c6->ddckCKDestBlt     = a->ddckCKDestBlt;
    c6->ddckCKSrcOverlay  = a->ddckCKSrcOverlay;
    c6->ddckCKSrcBlt      = a->ddckCKSrcBlt;
    c6->ddpfPixelFormat   = a->ddpfPixelFormat;
    c6->ddsCaps.dwCaps    = Ce5CapsToC6(a->ddsCaps.dwCaps);
}

/* Copy back the fields DDGPE fills as return values (lPitch/lpSurface/dwSurfaceSize
   into the union slots + caps/format the CE5 runtime reads after the call). */
static void Ce5DescFromC6(Ce5_DDSURFACEDESC* a, const DDSURFACEDESC* c6) {
    /* Strip ce6-only DDSD bits: ce6 DDSD_XPITCH (0x10) is undefined in CE5 and ce6
       DDSD_SURFACESIZE (0x80000) is CE5 DDSD_LINEARSIZE, which marks the primary as
       a formless linear chunk with invalid pitch and makes the CE5 runtime reject it. */
    a->dwFlags         = c6->dwFlags & ~(0x00000010u | 0x00080000u);
    a->dwHeight        = c6->dwHeight;
    a->dwWidth         = c6->dwWidth;
    a->u1.lPitch       = c6->lPitch;
    a->lpSurface       = c6->lpSurface;
    a->ddpfPixelFormat = c6->ddpfPixelFormat;
    a->ddsCaps.dwCaps  = Ce5CapsAddPlacementFromC6(a->ddsCaps.dwCaps, c6->ddsCaps.dwCaps);
}

/* CE5 CreateSurface: reshape the CE5 desc into the CE6 shape DDGPECreateSurface
   reads, run it, copy DDGPE's return fields back into the CE5 desc. */
static DWORD WINAPI Ce5CreateSurfaceWrap(Ce5_DDHAL_CREATESURFACEDATA* pd) {
    Ce5_DDSURFACEDESC* ce5sd = (Ce5_DDSURFACEDESC*)pd->lpDDSurfaceDesc;
    DDSURFACEDESC c6sd;
    if (ce5sd) {
        Ce5DescToC6(ce5sd, &c6sd);
        CERF_LOG_X_DEV("cerf_guest: Ce5 CS req ddsCaps", c6sd.ddsCaps.dwCaps);
        CERF_LOG_X_DEV("cerf_guest: Ce5 CS req dwFlags", c6sd.dwFlags);
    }
    DDHAL_CREATESURFACEDATA c6;
    c6.lpDD           = (LPDDRAWI_DIRECTDRAW_GBL)pd->lpDD;
    c6.lpDDSurfaceDesc= ce5sd ? &c6sd : NULL;
    c6.dwSCnt         = pd->dwSCnt;
    c6.lplpSList      = (LPDDRAWI_DDRAWSURFACE_LCL*)pd->lplpSList;
    c6.ddRVal         = DD_OK;
    /* Marshal each new surface's CE5 LCL into the ce6 shape DDGPE reads: caps
       (0x20->0x8, value-remapped) + the binding slot (lpAttachListFrom@0x10 saved
       for Ce5LclLeave to restore after persisting the DDGPESurf DDGPE writes). */
    void** sl = (void**)pd->lplpSList;
    DWORD  n  = (pd->dwSCnt < 8u) ? pd->dwSCnt : 8u;
    ULONG_PTR saved10[8] = { 0 };
    Ce5LclSave savedCaps[8] = { 0 };
    for (DWORD i = 0; i < n && sl; ++i)
        if (sl[i]) {
            saved10[i] = *(ULONG_PTR*)((BYTE*)sl[i] + CE5_LCL_DWRESERVED1_OFF);
            Ce5CreateMarshalIn(sl[i], &savedCaps[i]);
        }
    DWORD r = DDGPECreateSurface(&c6);
    for (DWORD i = 0; i < n && sl; ++i)
        if (sl[i]) {
            Ce5LclLeave(sl[i], saved10[i]);
            Ce5CreateMarshalOut(sl[i], &savedCaps[i]);
            /* DDGPE leaves lpGbl->fpVidMem 0; populate it from the bound DDGPESurf's
               buffer so the CE5 runtime sees the surface as having pixel memory. */
            ULONG gbl = *(ULONG*)((BYTE*)sl[i] + 0x04u);          /* CE5 LCL+0x04 = lpGbl */
            ULONG_PTR* bind = Ce5BindFind(sl[i]);
            ULONG bufva = CerfDDGPESurfBufferVa(bind ? (ULONG)*bind : 0u);
            if (gbl && bufva) *(ULONG*)((BYTE*)(ULONG_PTR)gbl + 0x14u) = bufva;
            CERF_LOG_X_DEV("cerf_guest: Ce5 CS surf fpVidMem set", bufva);
        }
    if (ce5sd) Ce5DescFromC6(ce5sd, &c6sd);
    pd->ddRVal = c6.ddRVal;
    CERF_LOG_X_DEV("cerf_guest: Ce5 CreateSurface ddRVal", (DWORD)c6.ddRVal);
    return r;
}

/* The CanCreate wrapper (lpDD, lpDDSurfaceDesc, bIsDifferentPixelFormat, ddRVal) is
   version-stable; the DDSURFACEDESC it points at is the CE5/CE6 skew, so reshape it the
   same way as CreateSurface before DDGPECanCreateSurface reads its pixel format. */
static DWORD WINAPI Ce5CanCreateSurfaceWrap(Ce5_DDHAL_CREATESURFACEDATA* pd) {
    LPDDHAL_CANCREATESURFACEDATA in = (LPDDHAL_CANCREATESURFACEDATA)pd;
    Ce5_DDSURFACEDESC* ce5sd = (Ce5_DDSURFACEDESC*)in->lpDDSurfaceDesc;
    DDSURFACEDESC c6sd;
    DDHAL_CANCREATESURFACEDATA c6;
    memset(&c6, 0, sizeof(c6));
    c6.lpDD                    = in->lpDD;
    c6.bIsDifferentPixelFormat = in->bIsDifferentPixelFormat;
    if (ce5sd) { Ce5DescToC6(ce5sd, &c6sd); c6.lpDDSurfaceDesc = &c6sd; }
    else       { c6.lpDDSurfaceDesc = NULL; }
    c6.ddRVal = DD_OK;
    DWORD r = DDGPECanCreateSurface(&c6);
    in->ddRVal = c6.ddRVal;
    CERF_LOG_X_DEV("cerf_guest: Ce5 CanCreateSurface ddRVal", (DWORD)c6.ddRVal);
    return r;
}

/* CE5 LockData puts dwFlags at the end (after a private callback ptr) where CE6
   has it right after rArea (ce5-oak ddrawi.h): translate, then run the FB-PA lock
   window mapper, copy the mapped pointer + ddRVal back. */
static DWORD WINAPI Ce5LockWrap(Ce5_DDHAL_LOCKDATA* pd) {
    DDHAL_LOCKDATA c6;
    memset(&c6, 0, sizeof(c6));
    c6.lpDD       = (LPDDRAWI_DIRECTDRAW_GBL)pd->lpDD;
    c6.lpDDSurface= (LPDDRAWI_DDRAWSURFACE_LCL)pd->lpDDSurface;
    c6.bHasRect   = pd->bHasRect;
    memcpy(&c6.rArea, &pd->rArea, sizeof(c6.rArea));  /* RECT<-RECTL, same layout */
    c6.dwFlags    = pd->dwFlags;
    ULONG_PTR sv = Ce5LclEnter(pd->lpDDSurface);
    DWORD r = CerfDDGPELockWrap(&c6);
    Ce5LclLeave(pd->lpDDSurface, sv);
    pd->lpSurfData = c6.lpSurfData;
    pd->ddRVal     = c6.ddRVal;
    CERF_LOG_X_DEV("cerf_guest: Ce5 Lock ddRVal", (DWORD)c6.ddRVal);
    return r;
}

extern "C" int CerfDDrawBlt(void* dstLcl, void* srcLcl, const RECTL* rDest,
                            const RECTL* rSrc, unsigned long ddFlags,
                            unsigned long ropArg, unsigned long fillColor,
                            unsigned long srcKeyOverride);

/* CE5 _DDHAL_BLTDATA head (ce5-oak ddrawi.h:1965). Only the head is read + ddRVal
   written; the runtime owns the trailing clip/alpha fields. */
typedef struct _Ce5_DDHAL_BLTDATA {
    PVOID   lpDD;
    PVOID   lpDDDestSurface;
    RECTL   rDest;
    PVOID   lpDDSrcSurface;
    RECTL   rSrc;
    DWORD   dwFlags;
    DWORD   dwROPFlags;
    DDBLTFX bltFX;
    HRESULT ddRVal;
} Ce5_DDHAL_BLTDATA;

static DWORD WINAPI Ce5BltWrap(Ce5_DDHAL_BLTDATA* pd) {
    /* CerfDDrawBlt resolves both surfaces via DDGPESurf::GetDDGPESurf
       (lcl->dwReserved1 at ce6 0x10); bind the CE5 side-map for dst + src around
       the call. Ce5LclEnter/Leave no-op on a NULL lcl (e.g. a color-fill src). */
    CERF_LOG_X_DEV("cerf_guest: Ce5 Blt dwFlags", pd->dwFlags);
    CERF_LOG_X_DEV("cerf_guest: Ce5 Blt dwROPFlags", pd->dwROPFlags);
    CERF_LOG_X_DEV("cerf_guest: Ce5 Blt bltFX.dwROP", pd->bltFX.dwROP);
    ULONG_PTR svD = Ce5LclEnter(pd->lpDDDestSurface);
    ULONG_PTR svS = Ce5LclEnter(pd->lpDDSrcSurface);
    int ok = CerfDDrawBlt(pd->lpDDDestSurface, pd->lpDDSrcSurface,
                          &pd->rDest, &pd->rSrc, pd->dwFlags,
                          pd->bltFX.dwROP, pd->bltFX.dwFillColor,
                          pd->bltFX.ddckSrcColorkey.dwColorSpaceLowValue);
    Ce5LclLeave(pd->lpDDSrcSurface, svS);
    Ce5LclLeave(pd->lpDDDestSurface, svD);
    pd->ddRVal = ok ? DD_OK : DDERR_GENERIC;
    CERF_LOG_X_DEV("cerf_guest: Ce5 Blt ddRVal", (DWORD)pd->ddRVal);
    return DDHAL_DRIVER_HANDLED;
}

/* These DDGPE bodies resolve the surface via GetDDGPESurf (lcl->dwReserved1), so
   the CE5 side-map must be bound across the call (see Ce5LclEnter). */
static DWORD WINAPI Ce5DestroySurfaceWrap(LPDDHAL_DESTROYSURFACEDATA pd) {
    ULONG_PTR sv = Ce5LclEnter(pd->lpDDSurface);
    DWORD r = DDGPEDestroySurface(pd);
    Ce5LclLeave(pd->lpDDSurface, sv);
    Ce5BindForget(pd->lpDDSurface);
    CERF_LOG_X_DEV("cerf_guest: Ce5 DestroySurface ddRVal", (DWORD)pd->ddRVal);
    return r;
}

static DWORD WINAPI Ce5FlipWrap(LPDDHAL_FLIPDATA pd) {
    /* Bind both surfaces; restore LIFO so a degenerate curr==targ flip still
       leaves lcl+0x10 holding the original CE5 lpAttachListFrom. */
    ULONG_PTR svC = Ce5LclEnter(pd->lpSurfCurr);
    ULONG_PTR svT = Ce5LclEnter(pd->lpSurfTarg);
    DWORD r = DDGPEFlip(pd);
    Ce5LclLeave(pd->lpSurfTarg, svT);
    Ce5LclLeave(pd->lpSurfCurr, svC);
    CERF_LOG_X_DEV("cerf_guest: Ce5 Flip ddRVal", (DWORD)pd->ddRVal);
    return r;
}

static DWORD WINAPI Ce5SetColorKeyWrap(LPDDHAL_SETCOLORKEYDATA pd) {
    ULONG_PTR sv = Ce5LclEnter(pd->lpDDSurface);
    DWORD r = DDGPESetColorKey(pd);
    Ce5LclLeave(pd->lpDDSurface, sv);
    CERF_LOG_X_DEV("cerf_guest: Ce5 SetColorKey ddRVal", (DWORD)pd->ddRVal);
    return r;
}

/* CE5 WAITFORVERTICALBLANKDATA has an extra hEvent before ddRVal (ce5-oak
   ddrawi.h:2225), placing ddRVal at 0x14 vs the ce6 0xC the DDGPE func writes;
   reshape to the ce6 head, delegate, copy the result back. */
typedef struct _Ce5_DDHAL_WAITFORVERTICALBLANKDATA {
    PVOID     lpDD;
    DWORD     dwFlags;
    DWORD     bIsInVB;
    ULONG_PTR hEvent;
    HRESULT   ddRVal;
} Ce5_DDHAL_WAITFORVERTICALBLANKDATA;

static DWORD WINAPI Ce5WaitForVBlankWrap(Ce5_DDHAL_WAITFORVERTICALBLANKDATA* pd) {
    DDHAL_WAITFORVERTICALBLANKDATA c6;
    memset(&c6, 0, sizeof(c6));
    c6.lpDD    = (LPDDRAWI_DIRECTDRAW_GBL)pd->lpDD;
    c6.dwFlags = pd->dwFlags;
    DWORD r = DDGPEWaitForVerticalBlank(&c6);
    pd->bIsInVB = c6.bIsInVB;
    pd->ddRVal  = c6.ddRVal;
    return r;
}

/* DDHAL_CB32_* / DDHAL_SURFCB32_* present-flag values per ce5-oak ddrawi.h
   (these bit positions differ from CE6). */
static Ce5_DDHAL_DDCALLBACKS g_ce5DDCallbacks = {
    sizeof(Ce5_DDHAL_DDCALLBACKS),
    /* CB32_CREATESURFACE|CANCREATESURFACE|WAITFORVERTICALBLANK|CREATEPALETTE */
    0x00000002u | 0x00000020u | 0x00000010u | 0x00000040u,
    NULL,                        /* DestroyDriver */
    (PVOID)Ce5CreateSurfaceWrap, /* CreateSurface */
    NULL,                        /* SetColorKey */
    NULL,                        /* SetMode */
    (PVOID)Ce5WaitForVBlankWrap, /* WaitForVerticalBlank */
    (PVOID)Ce5CanCreateSurfaceWrap,
    (PVOID)DDGPECreatePalette,   /* CreatePalette (writes only ddRVal@0xC) */
    NULL,                        /* GetScanLine */
    NULL,                        /* SetExclusiveMode */
    NULL                         /* FlipToGDISurface */
};

static Ce5_DDHAL_DDSURFACECALLBACKS g_ce5SurfCallbacks = {
    sizeof(Ce5_DDHAL_DDSURFACECALLBACKS),
    /* SURFCB32_DESTROYSURFACE|FLIP|BLT|LOCK|UNLOCK|SETCOLORKEY|GETBLTSTATUS|GETFLIPSTATUS|SETPALETTE */
    0x1u | 0x2u | 0x8u | 0x10u | 0x20u | 0x40u | 0x100u | 0x200u | 0x2000u,
    (PVOID)Ce5DestroySurfaceWrap,/* DestroySurface (LCL side-map) */
    (PVOID)Ce5FlipWrap,          /* Flip (LCL side-map, 2 surfaces) */
    NULL,                        /* SetClipList */
    (PVOID)Ce5LockWrap,          /* Lock (reshaped + LCL side-map) */
    (PVOID)CerfDDGPEUnlockWrap,  /* Unlock (no LCL deref) */
    (PVOID)Ce5BltWrap,           /* Blt -> host accelerator (LCL side-map) */
    (PVOID)Ce5SetColorKeyWrap,   /* SetColorKey (LCL side-map) */
    NULL,                        /* AddAttachedSurface */
    (PVOID)CerfGetBltStatus,     /* GetBltStatus (synchronous -> DD_OK) */
    (PVOID)DDGPEGetFlipStatus,   /* GetFlipStatus */
    NULL,                        /* UpdateOverlay */
    NULL,                        /* SetOverlayPosition */
    NULL,                        /* reserved4 */
    (PVOID)DDGPESetPalette,      /* SetPalette */
    NULL,                        /* reserved5 — 72B size pad */
    NULL                         /* reserved6 — 72B size pad */
};

/* The CE5/Zune ddcore decodes ddsCaps.dwCaps with CE5 bit numbering, which the
   ce6-oak ddraw.h this TU compiles against renumbers; its align-caps gate then
   reads the ce6 PRIMARYSURFACE/SYSTEMMEMORY bits as ce5 OFFSCREENPLAIN/OVERLAY and
   rejects them for lacking alignments. CE5 numeric values per ce5-standard ddraw.h. */
#define CE5_DDSCAPS_BACKBUFFER     0x00000004u
#define CE5_DDSCAPS_FLIP           0x00000010u
#define CE5_DDSCAPS_FRONTBUFFER    0x00000020u
#define CE5_DDSCAPS_PRIMARYSURFACE 0x00000200u
#define CE5_DDSCAPS_SYSTEMMEMORY   0x00000800u
#define CE5_DDSCAPS_VIDEOMEMORY    0x00004000u

/* Fill the CE5 DDHALINFO, mirroring stock ddraw_ipu_sdc.dll sub_318FE90: dwSize
   460, callbacks, fpPrimary, dwNumHeaps=0/pvmList=0 (HAL owns video memory), real
   dwVidMemTotal/Free, GetDriverInfo. Returns FALSE if our struct layout drifted
   from the 460-byte CE5 shape (loud, not silent). */
extern "C" BOOL Ce5HALInit(void* lpddhi) {
    if (sizeof(Ce5_DDHALINFO) != 460u) {
        CERF_LOG_X("cerf_guest: Ce5HALInit BAD sizeof(Ce5_DDHALINFO)",
                   (DWORD)sizeof(Ce5_DDHALINFO));
        return FALSE;
    }
    unsigned long vidBase = 0, vidSize = 0, vidFree = 0;
    CerfGetVideoMem(&vidBase, &vidSize, &vidFree);

    Ce5_DDHALINFO* h = (Ce5_DDHALINFO*)lpddhi;
    memset(h, 0, sizeof(Ce5_DDHALINFO));
    h->dwSize               = sizeof(Ce5_DDHALINFO);
    h->lpDDCallbacks        = &g_ce5DDCallbacks;
    h->lpDDSurfaceCallbacks = &g_ce5SurfCallbacks;
    h->lpDDPaletteCallbacks = NULL;

    h->vmiData.fpPrimary       = CerfGpeFbMemBasePa();
    h->vmiData.dwDisplayWidth  = g_FbWidth;
    h->vmiData.dwDisplayHeight = g_FbHeight;
    h->vmiData.lDisplayPitch   = (LONG)g_FbStride;
    h->vmiData.ddpfDisplay.dwSize        = sizeof(Ce5_DDPIXELFORMAT);
    h->vmiData.ddpfDisplay.dwFlags       = DDPF_RGB;
    h->vmiData.ddpfDisplay.dwRGBBitCount = g_FbBpp;
    if (g_FbBpp == 16) {
        h->vmiData.ddpfDisplay.dwRBitMask = 0xF800u;
        h->vmiData.ddpfDisplay.dwGBitMask = 0x07E0u;
        h->vmiData.ddpfDisplay.dwBBitMask = 0x001Fu;
    } else {
        h->vmiData.ddpfDisplay.dwRBitMask = 0x00FF0000u;
        h->vmiData.ddpfDisplay.dwGBitMask = 0x0000FF00u;
        h->vmiData.ddpfDisplay.dwBBitMask = 0x000000FFu;
    }
    h->vmiData.dwNumHeaps = 0;     /* HAL owns all video memory (matches stock) */
    h->vmiData.pvmList    = 0;

    h->ddCaps.dwSize        = sizeof(Ce5_DDCORECAPS);
    h->ddCaps.dwVidMemTotal = vidSize;
    h->ddCaps.dwVidMemFree  = vidFree;
    /* Without DDCAPS_CANBLTSYSMEM, ddcore (sub_376A6B4) routes a system-memory-
       source blit to the ddhel software path, which CPU-writes the FB-PA primary
       and data-aborts -> present fails DDERR_EXCEPTION. Numeric (ce42 ddraw.h
       2331/2460): the ce6-oak ddraw.h the driver builds against omits DDCAPS_*. */
    h->ddCaps.dwCaps = 0x40u /* DDCAPS_BLT */ | 0x80000000u /* DDCAPS_CANBLTSYSMEM */;
    h->ddCaps.ddsCaps.dwCaps =
        CE5_DDSCAPS_PRIMARYSURFACE | CE5_DDSCAPS_FRONTBUFFER | CE5_DDSCAPS_BACKBUFFER |
        CE5_DDSCAPS_FLIP | CE5_DDSCAPS_SYSTEMMEMORY | CE5_DDSCAPS_VIDEOMEMORY;
    h->ddCaps.dwCKeyCaps = DDCKEYCAPS_SRCBLT;
    SETROPBIT(h->ddCaps.dwRops, SRCCOPY);
    SETROPBIT(h->ddCaps.dwRops, PATCOPY);
    SETROPBIT(h->ddCaps.dwRops, BLACKNESS);
    SETROPBIT(h->ddCaps.dwRops, WHITENESS);

    /* One mode = the active framebuffer; masks mirror ddpfDisplay. The body is
       per-process manual-mapped, so this static is per-process and the CE5 loader
       reads it synchronously inside this HALInit call (same process). */
    static Ce5_DDHALMODEINFO s_modeInfo;
    s_modeInfo.dwWidth        = g_FbWidth;
    s_modeInfo.dwHeight       = g_FbHeight;
    s_modeInfo.lPitch         = (LONG)g_FbStride;
    s_modeInfo.dwBPP          = g_FbBpp;
    s_modeInfo.wFlags         = 0;
    s_modeInfo.wRefreshRate   = 60;
    s_modeInfo.dwRBitMask     = h->vmiData.ddpfDisplay.dwRBitMask;
    s_modeInfo.dwGBitMask     = h->vmiData.ddpfDisplay.dwGBitMask;
    s_modeInfo.dwBBitMask     = h->vmiData.ddpfDisplay.dwBBitMask;
    s_modeInfo.dwAlphaBitMask = 0;
    h->dwNumModes = 1;
    h->lpModeInfo = &s_modeInfo;

    h->GetDriverInfo = (PVOID)CerfHalGetDriverInfo;
    return TRUE;
}
