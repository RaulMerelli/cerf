#include <windows.h>
#include <pkfuncs.h>
#include <winddi.h>
#include <gpe.h>

#include "cerf_regs_map.h"

#ifndef GCAPS_GRAY16
#define GCAPS_GRAY16 0x01000000u   /* winddi.h:294 */
#endif

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

#define CERF_GPE_DESC_VA          0x000u
#define CERF_GPE_STATUS           0x004u
#define CERF_GPE_KICK_OFFSET      0x800u
#define CERF_GPE_GRAD_KICK_OFFSET 0x804u
#define CERF_GPE_LINE_KICK_OFFSET 0x808u
#define CERF_GPE_STATUS_DONE      2u

static volatile ULONG* s_fb_regs  = NULL;
static volatile ULONG* s_gpe_cmd  = NULL;
ULONG g_FbWidth   = 0;
ULONG g_FbHeight  = 0;
ULONG g_FbBpp     = 0;
ULONG g_FbStride  = 0;
ULONG g_FbMemPa   = 0;
ULONG g_FbMemTotal = 0;   /* total host-backed region bytes (kFbRegMemSizeTotal);
                             region tail past the primary = DDraw video memory. */
ULONG g_FbPrimaryReserve = 0;  /* kFbRegPrimaryReserve: byte span the host reserved
                                  for the growable primary; video heap starts past it. */
void* g_FbMemVa   = NULL;
ULONG g_EngineVersion = 0;

/* The filename cerf_guest was injected under (the stock display driver it
   replaced); the driver-in-driver carrier needs it as the Dll value device.exe
   LoadLibrarys to reach the CDD_ entry points in this same module. */
static wchar_t s_module_name[MAX_PATH] = { 0 };
static HMODULE s_hinst = NULL;   /* captured in DllMain, used by the display init */
extern "C" const wchar_t* CerfInjectedModuleName(void) { return s_module_name; }

/* A stub-mapped body has no real module handle, so its DllMain
   GetModuleFileNameW yields nothing; the stub supplies its own (ROM, trusted)
   name here for driver-in-driver to register as the Dll device.exe loads. */
extern "C" void CerfSetCarrierName(const wchar_t* name) {
    if (!name) return;
    int i = 0;
    for (; name[i] && i < MAX_PATH - 1; ++i) s_module_name[i] = name[i];
    s_module_name[i] = 0;
}

void CerfReadFbRegs(void) {
    if (s_fb_regs) return;
    s_fb_regs = (volatile ULONG*)CerfMapRegsPage(CerfVirt::kFramebufferRegsBase,
                                                 CerfVirt::kFramebufferRegsSize);
    if (!s_fb_regs) return;
    g_FbWidth  = s_fb_regs[0];
    g_FbHeight = s_fb_regs[1];
    g_FbBpp    = s_fb_regs[2];
    g_FbStride = s_fb_regs[3];
    g_FbMemPa   = s_fb_regs[5];   /* kFbRegMemBasePa   (0x14 >> 2) */
    g_FbMemTotal = s_fb_regs[7];  /* kFbRegMemSizeTotal (0x1C >> 2) */
    g_FbPrimaryReserve = s_fb_regs[8];  /* kFbRegPrimaryReserve (0x20 >> 2) */
}

BOOL CerfMapGpeCmd(void) {
    CERF_LOG_DEV("cerf_guest: CerfMapGpeCmd entry");
    if (s_gpe_cmd) return TRUE;
    s_gpe_cmd = (volatile ULONG*)CerfMapRegsPage(CerfVirt::kGpeCmdBase,
                                                 CerfVirt::kGpeCmdSize);
    return s_gpe_cmd != NULL;
}

/* Publish the guest VA of a filled CerfBltDescriptor and kick; the host reads
   it through the live MMU, runs the blit, and returns the status word. */
extern "C" ULONG CerfGpeBlt(ULONG desc_va) {
    if (!CerfMapGpeCmd()) return (ULONG)-1;
    s_gpe_cmd[CERF_GPE_DESC_VA / 4] = desc_va;
    *(volatile ULONG*)(((volatile UCHAR*)s_gpe_cmd) + CERF_GPE_KICK_OFFSET) = 1u;
    return s_gpe_cmd[CERF_GPE_STATUS / 4];
}

/* Publish a filled CerfGradDescriptor and kick the gradient port; the host
   reads it through the live MMU, paints the ramp, and returns the status. */
extern "C" ULONG CerfGpeGrad(ULONG desc_va) {
    if (!CerfMapGpeCmd()) return (ULONG)-1;
    s_gpe_cmd[CERF_GPE_DESC_VA / 4] = desc_va;
    *(volatile ULONG*)(((volatile UCHAR*)s_gpe_cmd) + CERF_GPE_GRAD_KICK_OFFSET) = 1u;
    return s_gpe_cmd[CERF_GPE_STATUS / 4];
}

/* Publish a filled CerfLineDescriptor and kick the line port; the host reads it
   through the live MMU, draws the segment by PA, and returns the status. */
extern "C" ULONG CerfGpeLine(ULONG desc_va) {
    if (!CerfMapGpeCmd()) return (ULONG)-1;
    s_gpe_cmd[CERF_GPE_DESC_VA / 4] = desc_va;
    *(volatile ULONG*)(((volatile UCHAR*)s_gpe_cmd) + CERF_GPE_LINE_KICK_OFFSET) = 1u;
    return s_gpe_cmd[CERF_GPE_STATUS / 4];
}

extern "C" ULONG CerfGpeFbMemBasePa(void) { return CerfVirt::kFramebufferMemBase; }

/* The host accelerator addresses the framebuffer by physical address
   (SurfaceFbPa), so the surface base is the FB PA itself, not a mapped VA. */
void* CerfMapFbMemory(void) {
    if (g_FbMemVa) return g_FbMemVa;
    if (g_FbMemPa == 0 || g_FbStride == 0 || g_FbHeight == 0) return NULL;
    if (g_FbMemTotal < g_FbStride * g_FbHeight) {
        CERF_LOG_X("cerf_guest: CerfMapFbMemory region too small for primary; total", g_FbMemTotal);
        return NULL;
    }
    g_FbMemVa = (void*)(ULONG_PTR)g_FbMemPa;
    return g_FbMemVa;
}

/* On-demand FB aperture for guest CPU code that must touch PA-only FB pixels
   (DirectDraw Lock, GDI SW-blit edge). Map only the rect needed — a whole 4K
   surface (33 MB) exceeds the 32 MB slot. Returns the exact byte; free the same
   pointer via CerfUnmapFbWindow. */
extern "C" void* CerfMapFbWindow(ULONG fb_pa, ULONG bytes) {
    const ULONG page_off  = fb_pa & 0xFFFu;
    const ULONG base_pa   = fb_pa & ~0xFFFu;
    const ULONG map_bytes = (page_off + bytes + 0xFFFu) & ~0xFFFu;
    void* va = VirtualAlloc(0, map_bytes, MEM_RESERVE, PAGE_NOACCESS);
    if (!va) return NULL;
    if (!VirtualCopy(va, (LPVOID)(base_pa >> 8), map_bytes,
                     PAGE_READWRITE | PAGE_NOCACHE | PAGE_PHYSICAL)) {
        VirtualFree(va, 0, MEM_RELEASE);
        return NULL;
    }
    return (void*)((BYTE*)va + page_off);
}

extern "C" void CerfUnmapFbWindow(void* exact_va) {
    if (exact_va) VirtualFree((void*)((ULONG_PTR)exact_va & ~0xFFFu), 0, MEM_RELEASE);
}

/* The DirectDraw HAL is shared across processes (devemu_ce5 ddcore keeps one
   driver object list, DDrawDriverObjectListMutex), so the surface Lock VA must be
   valid in every process. A VirtualAlloc(MEM_RESERVE, PAGE_NOACCESS) of >= 2MB
   lands in CE's user-writable large-memory region, mapped identically in all
   processes (CE5 virtmem.c DoVirtualAlloc -> HugeVirtualReserve); the stock devemu
   driver relies on the same threshold (gpeflat.cpp:273 VirtualAlloc(0,
   max(fbSize, 2*1024*1024), ...)). The whole 32MB FB region clears it. */
static void* g_FbGlobalVa = NULL;
extern "C" void* CerfMapFbGlobal(void) {
    if (g_FbGlobalVa) return g_FbGlobalVa;
    const ULONG base = CerfGpeFbMemBasePa();
    if (base == 0 || g_FbMemTotal < 0x200000u) return NULL;  /* < 2MB never global */
    g_FbGlobalVa = CerfMapFbWindow(base, g_FbMemTotal);
    return g_FbGlobalVa;
}

static ULONG s_RgbMasks_32bpp[3] = { 0x00FF0000u, 0x0000FF00u, 0x000000FFu };
static ULONG s_RgbMasks_16bpp[3] = { 0xF800u,     0x07E0u,     0x001Fu     };

extern "C" ULONG* APIENTRY DrvGetMasks(DHPDEV) {
    CERF_LOG_X_DEV("cerf_guest: DrvGetMasks bpp", g_FbBpp);
    return (g_FbBpp == 16) ? s_RgbMasks_16bpp
         : (g_FbBpp == 32) ? s_RgbMasks_32bpp : NULL;
}

extern "C" BOOL APIENTRY DrvEndDoc(SURFOBJ*, FLONG)            { CERF_LOG_DEV("cerf_guest: DrvEndDoc"); return TRUE; }
extern "C" BOOL APIENTRY DrvStartDoc(SURFOBJ*, PWSTR, DWORD)   { CERF_LOG_DEV("cerf_guest: DrvStartDoc"); return TRUE; }
extern "C" BOOL APIENTRY DrvStartPage(SURFOBJ*)                { CERF_LOG_DEV("cerf_guest: DrvStartPage"); return TRUE; }
extern "C" BOOL APIENTRY DrvExclusiveMode(DHPDEV, BOOL)        { CERF_LOG_DEV("cerf_guest: DrvExclusiveMode"); return TRUE; }

extern "C" BOOL APIENTRY DrvBitBlt(SURFOBJ*, SURFOBJ*, SURFOBJ*, CLIPOBJ*,
                                    XLATEOBJ*, RECTL*, POINTL*, POINTL*,
                                    BRUSHOBJ*, POINTL*, ROP4);
extern "C" BOOL APIENTRY DrvCopyBits(SURFOBJ*, SURFOBJ*, CLIPOBJ*, XLATEOBJ*,
                                      RECTL*, POINTL*);
extern "C" BOOL APIENTRY DrvAnyBlt(SURFOBJ*, SURFOBJ*, SURFOBJ*, CLIPOBJ*,
                                    XLATEOBJ*, POINTL*, RECTL*, RECTL*, POINTL*,
                                    BRUSHOBJ*, POINTL*, ROP4, ULONG, ULONG);
extern "C" BOOL APIENTRY DrvTransparentBlt(SURFOBJ*, SURFOBJ*, CLIPOBJ*,
                                            XLATEOBJ*, RECTL*, RECTL*, ULONG);
extern "C" BOOL APIENTRY DrvRealizeBrush(BRUSHOBJ*, SURFOBJ*, SURFOBJ*, SURFOBJ*,
                                          XLATEOBJ*, ULONG);
extern "C" BOOL APIENTRY DrvStrokePath(SURFOBJ*, PATHOBJ*, CLIPOBJ*, XFORMOBJ*,
                                        BRUSHOBJ*, POINTL*, LINEATTRS*, MIX);
extern "C" BOOL APIENTRY DrvFillPath(SURFOBJ*, PATHOBJ*, CLIPOBJ*, BRUSHOBJ*,
                                      POINTL*, MIX, FLONG);
extern "C" BOOL APIENTRY DrvPaint(SURFOBJ*, CLIPOBJ*, BRUSHOBJ*, POINTL*, MIX);

extern "C" BOOL APIENTRY CerfDrvGradientFill(SURFOBJ*, CLIPOBJ*, XLATEOBJ*,
                                              TRIVERTEX*, ULONG, PVOID, ULONG,
                                              RECTL*, POINTL*, ULONG);
extern "C" BOOL APIENTRY CerfDrvAlphaBlend(SURFOBJ*, SURFOBJ*, CLIPOBJ*,
                                            XLATEOBJ*, RECTL*, RECTL*, BLENDOBJ*);

static BOOL APIENTRY CerfTraceBitBlt(SURFOBJ* dst, SURFOBJ* src, SURFOBJ* msk,
                                      CLIPOBJ* co, XLATEOBJ* xlo, RECTL* prd,
                                      POINTL* pps, POINTL* ppm, BRUSHOBJ* pbo,
                                      POINTL* ppb, ROP4 rop4) {
    CERF_LOG_X_DEV("cerf_guest: DrvBitBlt rop4", rop4);
    CERF_LOG_X_DEV("cerf_guest: DrvBitBlt xlo",  xlo);
    CERF_LOG_X_DEV("cerf_guest: DrvBitBlt src",  src);
    CERF_LOG_X_DEV("cerf_guest: DrvBitBlt dst",  dst);
    CERF_LOG_X_DEV("cerf_guest: DrvBitBlt pbo",  pbo);
    return DrvBitBlt(dst, src, msk, co, xlo, prd, pps, ppm, pbo, ppb, rop4);
}
static BOOL APIENTRY CerfTraceCopyBits(SURFOBJ* dst, SURFOBJ* src, CLIPOBJ* co,
                                        XLATEOBJ* xlo, RECTL* prd, POINTL* pps) {
    CERF_LOG_X_DEV("cerf_guest: DrvCopyBits xlo", xlo);
    CERF_LOG_X_DEV("cerf_guest: DrvCopyBits src", src);
    CERF_LOG_X_DEV("cerf_guest: DrvCopyBits dst", dst);
    return DrvCopyBits(dst, src, co, xlo, prd, pps);
}
static BOOL APIENTRY CerfTraceAnyBlt(SURFOBJ* dst, SURFOBJ* src, SURFOBJ* msk,
                                      CLIPOBJ* co, XLATEOBJ* xlo, POINTL* phto,
                                      RECTL* prd, RECTL* prs, POINTL* ppm,
                                      BRUSHOBJ* pbo, POINTL* ppb, ROP4 rop4,
                                      ULONG mode, ULONG flags) {
    CERF_LOG_X_DEV("cerf_guest: DrvAnyBlt rop4", rop4);
    CERF_LOG_X_DEV("cerf_guest: DrvAnyBlt xlo",  xlo);
    CERF_LOG_X_DEV("cerf_guest: DrvAnyBlt mode", mode);
    return DrvAnyBlt(dst, src, msk, co, xlo, phto, prd, prs, ppm, pbo, ppb,
                      rop4, mode, flags);
}
static BOOL APIENTRY CerfTraceTransparentBlt(SURFOBJ* dst, SURFOBJ* src,
                                              CLIPOBJ* co, XLATEOBJ* xlo,
                                              RECTL* prd, RECTL* prs, ULONG tc) {
    CERF_LOG_X_DEV("cerf_guest: DrvTransparentBlt xlo", xlo);
    CERF_LOG_X_DEV("cerf_guest: DrvTransparentBlt tc",  tc);
    return DrvTransparentBlt(dst, src, co, xlo, prd, prs, tc);
}
static BOOL APIENTRY CerfTraceRealizeBrush(BRUSHOBJ* pbo, SURFOBJ* psoTarget,
                                            SURFOBJ* psoPattern, SURFOBJ* psoMask,
                                            XLATEOBJ* pxlo, ULONG iHatch) {
    CERF_LOG_X_DEV("cerf_guest: DrvRealizeBrush pxlo", pxlo);
    CERF_LOG_X_DEV("cerf_guest: DrvRealizeBrush iHatch", iHatch);
    return DrvRealizeBrush(pbo, psoTarget, psoPattern, psoMask, pxlo, iHatch);
}
static BOOL APIENTRY CerfTraceStrokePath(SURFOBJ* pso, PATHOBJ* ppo, CLIPOBJ* pco,
                                          XFORMOBJ* pxo, BRUSHOBJ* pbo,
                                          POINTL* pptlBrush, LINEATTRS* plineattrs,
                                          MIX mix) {
    CERF_LOG_X_DEV("cerf_guest: DrvStrokePath mix", mix);
    return DrvStrokePath(pso, ppo, pco, pxo, pbo, pptlBrush, plineattrs, mix);
}
static BOOL APIENTRY CerfTraceFillPath(SURFOBJ* pso, PATHOBJ* ppo, CLIPOBJ* pco,
                                        BRUSHOBJ* pbo, POINTL* pptlBrush, MIX mix,
                                        FLONG flOptions) {
    CERF_LOG_X_DEV("cerf_guest: DrvFillPath mix", mix);
    return DrvFillPath(pso, ppo, pco, pbo, pptlBrush, mix, flOptions);
}
static BOOL APIENTRY CerfTracePaint(SURFOBJ* pso, CLIPOBJ* pco, BRUSHOBJ* pbo,
                                     POINTL* pptlBrush, MIX mix) {
    CERF_LOG_X_DEV("cerf_guest: DrvPaint mix", mix);
    return DrvPaint(pso, pco, pbo, pptlBrush, mix);
}


/* DO NOT delegate TRIVIAL to the engine — CE3 gwes's static TRIVIAL XLATEOBJ
   has a bogus internal palette ptr the engine handler derefs → Data Abort.
   Trivial = no palette anyway, so 0 is correct. */
static ULONG (*g_EngineXlateObj_cGetPalette)(XLATEOBJ*, ULONG, ULONG, ULONG*) = NULL;

static ULONG WINAPI CerfXlateGetPaletteWrap(XLATEOBJ* pxlo, ULONG iPal,
                                             ULONG cPal, ULONG* pPal) {
    if (!pxlo) return 0;
    CERF_LOG_X_DEV("cerf_guest: XlateGetPal flXlate", pxlo->flXlate);
    if (pxlo->flXlate == XO_TRIVIAL) return 0;
    if (!g_EngineXlateObj_cGetPalette) return 0;
    return g_EngineXlateObj_cGetPalette(pxlo, iPal, cPal, pPal);
}


/* CE3/CE5/WM5 lack the engine palette pool (13-callback ENGCALLBACKS); these
   supply no-pool semantics for the 3 missing callbacks. DO NOT make Release a
   no-op — gpe.cpp:747 delegates the only free of lib's new ULONG[] palette
   here; a no-op leaks one buffer per brush-realize. */
static BOOL CerfNoPoolGetPalette(ULONG, ULONG**, int*)             { return FALSE; }
static VOID CerfNoPoolAddPalette(ULONG, ULONG*, int)              { }
static VOID CerfNoPoolReleasePalette(ULONG, ULONG* pPalette, int) { delete[] pPalette; }

/* Lib's DrvEnablePDEV writes pgdiinfo->ulVersion = DDI_DRIVER_VERSION (0x00040001
   = CE6) unconditionally. On CE3 (engine expects 0x00020001), this makes engine
   think driver is CE5+ → takes wrong code path. Wrapper patches ulVersion to
   the actual iEngineVersion the engine passed in. */
extern "C" DHPDEV APIENTRY DrvEnablePDEV(DEVMODEW*, LPWSTR, ULONG, HSURF*,
                                          ULONG, ULONG*, ULONG, DEVINFO*,
                                          HDEV, LPWSTR, HANDLE);

extern "C" void CerfStartPointerPump(void);
extern "C" void CerfStartKeyboardPump(void);
extern "C" void CerfStartResizePump(void);
extern "C" void CerfStartTaskManagerPump(void);
extern "C" void CerfStartDriverInDriver(void);
extern "C" void CerfAdvertiseDisplayPower(void);
extern "C" void CerfSetVidBackingByOsMajor(unsigned long os_major);

static DHPDEV APIENTRY CerfEnablePDEVWrap(
    DEVMODEW* pdm, LPWSTR pwszLogAddress, ULONG cPat, HSURF* phsurfPatterns,
    ULONG cjCaps, ULONG* pdevcaps, ULONG cjDevInfo, DEVINFO* pdi,
    HDEV hdev, LPWSTR pwszDeviceName, HANDLE hDriver) {
    DHPDEV result = DrvEnablePDEV(pdm, pwszLogAddress, cPat, phsurfPatterns,
                                   cjCaps, pdevcaps, cjDevInfo, pdi,
                                   hdev, pwszDeviceName, hDriver);
    if (result) CerfStartPointerPump();
    if (result) CerfStartKeyboardPump();
    if (result) CerfStartResizePump();
    if (result) CerfStartTaskManagerPump();
    if (result) CerfStartDriverInDriver();
    if (result) CerfAdvertiseDisplayPower();
    if (!result || !pdevcaps || cjCaps < sizeof(ULONG) || g_EngineVersion == 0) {
        return result;
    }
    pdevcaps[0] = g_EngineVersion;
    /* ulHorzRes/ulVertRes (GDIINFO words 4/5). On a runtime re-enable the gwes
       applier copies these into SM_CXSCREEN/CYSCREEN and the desktop surface
       rect, so reporting the live g_Fb* target here is what makes auto-resize
       take effect regardless of which DEVMODE the matched mode carried. */
    if (cjCaps >= 6 * sizeof(ULONG)) {
        pdevcaps[4] = g_FbWidth;
        pdevcaps[5] = g_FbHeight;
    }
    /* flTextCaps (GDIINFO word 12) GCAPS_GRAY16 makes GDI emit glyph coverage as a
       0xAAF0 + 4bpp-mask blit the host gamma-blends; the GPE lib arms this from
       AATextBltInit in its DrvEnableDriver (ddi_if.cpp:391/1279), which cerf_guest
       replaces, so set it here. bpp>8 matches the lib's gate (ddi_if.cpp:1263). */
    if (g_FbBpp > 8) {
        if (cjCaps >= 13 * sizeof(ULONG)) pdevcaps[12] |= GCAPS_GRAY16;
        if (pdi && cjDevInfo >= sizeof(DEVINFO)) pdi->flGraphicsCaps |= GCAPS_GRAY16;
    }
    if (g_EngineVersion == 0x00020001u && cjCaps >= 20 * sizeof(ULONG)) {
        /* CE3 GPE lib's exact GDIINFO fill (WINCE300 PRIVATE GPE DDI_IF.CPP:801-817);
           the linked CE6 lib wrote CE6 values at CE6 offsets, and CE3 has no
           flShadeBlendCaps so offset 52+ is shifted. RC_* per ce3 wingdi.h:220-231. */
        pdevcaps[2]  = 64u;
        pdevcaps[3]  = 60u;
        pdevcaps[9]  = 0x801u | ((g_FbBpp <= 8) ? 0x100u : 0u);
        pdevcaps[13] = 0u;
        pdevcaps[14] = 0u;
        pdevcaps[15] = 0u;
        pdevcaps[16] = 1u;
        pdevcaps[17] = 1u;
        pdevcaps[18] = 1u;
        pdevcaps[19] = 0u;
    }
    return result;
}

extern "C" BOOL APIENTRY DrvEnableDriver(ULONG iEngineVersion,
                                          ULONG cj,
                                          DRVENABLEDATA* pded,
                                          PENGCALLBACKS pCallbacks) {
    CERF_LOG_INIT(CERF_LOG_CH_DISPLAY);   /* arm before the first log */
    CERF_LOG_X("cerf_guest: DrvEnableDriver iEngineVersion", iEngineVersion);
    if (pded == NULL || pCallbacks == NULL || cj < 27 * sizeof(void*)) return FALSE;

    if (!s_module_name[0]) {
        wchar_t full[MAX_PATH];
        DWORD n = GetModuleFileNameW(s_hinst, full, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            const wchar_t* base = full;
            for (const wchar_t* p = full; *p; ++p)
                if (*p == L'\\' || *p == L'/') base = p + 1;
            ULONG i = 0;
            for (; base[i] && i < MAX_PATH - 1; ++i) s_module_name[i] = base[i];
            s_module_name[i] = L'\0';
        }
    }
    CerfReadFbRegs();

    /* Select the ddraw video-memory backing before any surface alloc: FCSE kernels
       (CE<=5) share one HAL across processes and need a cross-process-global FB VA;
       ASID kernels (CE6+) get a per-client-remapped guest-RAM heap. */
    {
        OSVERSIONINFOW ovi;
        ovi.dwOSVersionInfoSize = sizeof(ovi);
        CerfSetVidBackingByOsMajor(GetVersionExW(&ovi) ? ovi.dwMajorVersion : 5u);
    }

    g_EngineVersion = iEngineVersion;

    BRUSHOBJ_pvAllocRbrush  = pCallbacks->BRUSHOBJ_pvAllocRbrush;
    BRUSHOBJ_pvGetRbrush    = pCallbacks->BRUSHOBJ_pvGetRbrush;
    CLIPOBJ_cEnumStart      = pCallbacks->CLIPOBJ_cEnumStart;
    CLIPOBJ_bEnum           = pCallbacks->CLIPOBJ_bEnum;
    PALOBJ_cGetColors       = pCallbacks->PALOBJ_cGetColors;
    PATHOBJ_vEnumStart      = pCallbacks->PATHOBJ_vEnumStart;
    PATHOBJ_bEnum           = pCallbacks->PATHOBJ_bEnum;
    PATHOBJ_vGetBounds      = pCallbacks->PATHOBJ_vGetBounds;
    g_EngineXlateObj_cGetPalette = pCallbacks->XLATEOBJ_cGetPalette;
    XLATEOBJ_cGetPalette    = CerfXlateGetPaletteWrap;
    EngCreateDeviceSurface  = pCallbacks->EngCreateDeviceSurface;
    EngDeleteSurface        = pCallbacks->EngDeleteSurface;
    EngCreateDeviceBitmap   = pCallbacks->EngCreateDeviceBitmap;
    EngCreatePalette        = pCallbacks->EngCreatePalette;

    if (cj >= 31 * sizeof(void*)) {
        EngGetPaletteFromPool   = pCallbacks->EngGetPaletteFromPool;
        EngAddPaletteToPool     = pCallbacks->EngAddPaletteToPool;
        EngReleasePooledPalette = pCallbacks->EngReleasePooledPalette;
    } else {
        EngGetPaletteFromPool   = CerfNoPoolGetPalette;
        EngAddPaletteToPool     = CerfNoPoolAddPalette;
        EngReleasePooledPalette = CerfNoPoolReleasePalette;
    }

    memset(pded, 0, cj);
    pded->DrvEnablePDEV         = CerfEnablePDEVWrap;
    pded->DrvDisablePDEV        = DrvDisablePDEV;
    pded->DrvEnableSurface      = DrvEnableSurface;
    pded->DrvDisableSurface     = DrvDisableSurface;
    pded->DrvCreateDeviceBitmap = DrvCreateDeviceBitmap;
    pded->DrvDeleteDeviceBitmap = DrvDeleteDeviceBitmap;
    pded->DrvRealizeBrush       = CerfTraceRealizeBrush;
    pded->DrvStrokePath         = CerfTraceStrokePath;
    pded->DrvFillPath           = CerfTraceFillPath;
    pded->DrvPaint              = CerfTracePaint;
    pded->DrvBitBlt             = CerfTraceBitBlt;
    pded->DrvCopyBits           = CerfTraceCopyBits;
    pded->DrvAnyBlt             = CerfTraceAnyBlt;
    pded->DrvTransparentBlt     = CerfTraceTransparentBlt;
    pded->DrvSetPalette         = DrvSetPalette;
    pded->DrvSetPointerShape    = DrvSetPointerShape;
    pded->DrvMovePointer        = DrvMovePointer;
    pded->DrvGetModes           = DrvGetModes;
    pded->DrvRealizeColor       = DrvRealizeColor;
    pded->DrvGetMasks           = DrvGetMasks;
    pded->DrvUnrealizeColor     = DrvUnrealizeColor;
    pded->DrvContrastControl    = DrvContrastControl;
    pded->DrvPowerHandler       = DrvPowerHandler;
    pded->DrvEndDoc             = DrvEndDoc;
    pded->DrvStartDoc           = DrvStartDoc;
    pded->DrvStartPage          = DrvStartPage;
    pded->DrvEscape             = DrvEscape;
    /* Gradient/alpha gated exactly like DeviceEmulator_lcd (sub_17C6048):
       cj==120 -> both, cj==116 -> gradient only. A NULL slot 27 makes gwes
       SetLastError(ERROR_NOT_SUPPORTED) and paint nothing (sub_82378). */
    if (cj >= 30 * sizeof(void*)) {
        pded->DrvGradientFill   = CerfDrvGradientFill;
        pded->DrvAlphaBlend     = CerfDrvAlphaBlend;
        pded->DrvExclusiveMode  = DrvExclusiveMode;
    } else if (cj >= 29 * sizeof(void*)) {
        pded->DrvGradientFill   = CerfDrvGradientFill;
    }
    if (cj >= 31 * sizeof(void*)) {
        pded->DrvDisableDriver  = DrvDisableDriver;
    }
    return TRUE;
}

extern "C" BOOL APIENTRY DllEntryPoint(HANDLE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        CERF_LOG("cerf_guest: DLL_PROCESS_ATTACH");
        s_hinst = (HMODULE)hInst;
    } else if (reason == DLL_PROCESS_DETACH) {
        CERF_LOG("cerf_guest: DLL_PROCESS_DETACH");
    }
    return TRUE;
}
