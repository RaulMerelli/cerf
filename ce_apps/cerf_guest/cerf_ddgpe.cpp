#include "cerf_ddgpe.h"

/* Rotation-escape ABI values: ce6-oak pwingdi.h (escape codes) + ce4.2 wingdi.h
   (DMDO_* orientation flags, DM_DISPLAYORIENTATION). */
#ifndef QUERYESCSUPPORT
#define QUERYESCSUPPORT 8
#endif
#ifndef DRVESC_SETSCREENROTATION
#define DRVESC_SETSCREENROTATION 6301
#endif
#ifndef DRVESC_GETSCREENROTATION
#define DRVESC_GETSCREENROTATION 6302
#endif
#ifndef DMDO_0
#define DMDO_0   0
#endif
#ifndef DMDO_90
#define DMDO_90  1
#endif
#ifndef DMDO_180
#define DMDO_180 2
#endif
#ifndef DMDO_270
#define DMDO_270 4
#endif
#ifndef DISP_CHANGE_SUCCESSFUL
#define DISP_CHANGE_SUCCESSFUL 0
#endif

extern "C" int APIENTRY MulDiv(int a, int b, int c) {
    if (c == 0) return -1;
    __int64 prod = (__int64)a * (__int64)b;
    if (c < 0) { prod = -prod; c = -c; }
    prod += (prod >= 0) ? (c / 2) : -(c / 2);
    return (int)(prod / c);
}

static ULONG s_CerfRgbMasks_32bpp[3] = { 0x00FF0000u, 0x0000FF00u, 0x000000FFu };
static ULONG s_CerfRgbMasks_16bpp[3] = { 0xF800u,     0x07E0u,     0x001Fu     };

/* The generic DDGPESurf ctors never set m_fInVideoMemory; without setting it
   here, DDGPECreateSurface (ddhsurf.cpp:321) tags the surface SYSTEMMEMORY and
   the host HW-accel blit path never engages. */
class CerfVidSurf : public DDGPESurf {
public:
    CerfVidSurf(int w, int h, void* pBits, int stride, EGPEFormat fmt,
                EDDGPEPixelFormat pf, unsigned long offset, SurfaceHeap* node)
        : DDGPESurf(w, h, pBits, stride, fmt, pf), m_node(node) {
        m_fInVideoMemory       = 1;
        m_nOffsetInVideoMemory = offset;
    }
    virtual ~CerfVidSurf() {
        if (m_node) { m_node->Free(); m_node = NULL; }
    }
private:
    SurfaceHeap* m_node;
};

CerfDDGPE::CerfDDGPE() : DDGPE() {
    m_paletteEntries = 0;
    memset(m_palette, 0, sizeof(m_palette));
    memset(&m_gpeMode, 0, sizeof(m_gpeMode));
    m_pVidHeap  = NULL;
    m_vidBaseVa = NULL;
    m_vidBasePa = 0;
    m_vidSize   = 0;
    m_currentRotation = 0;  /* DMDO_0 */
}

/* Bring up the offscreen video-memory heap over the CERF fb region tail past
   the primary. Uses only the fb-reg dims (present from DLL attach), so it is
   callable at HALInit time, before SetMode allocates the primary. */
bool CerfDDGPE::EnsureVideoHeap() {
    if (m_pVidHeap) return true;
    void* base = CerfMapFbMemory();
    if (!base) {
        CERF_LOG("cerf_guest: EnsureVideoHeap FB map FAILED");
        return false;
    }
    /* Reserve the host-declared max-primary span, not the CURRENT primary: the
       primary grows on auto-resize, and a heap placed after the boot primary
       would be overrun by a larger re-mode (icon/bitmap surfaces corrupt). */
    ULONG primary = g_FbPrimaryReserve ? g_FbPrimaryReserve
                                       : g_FbStride * g_FbHeight;
    if (g_FbMemTotal <= primary) {
        CERF_LOG_X("cerf_guest: EnsureVideoHeap no offscreen; memtotal", g_FbMemTotal);
        return false;
    }
    m_vidBaseVa = (BYTE*)base + primary;
    m_vidBasePa = CerfGpeFbMemBasePa() + primary;
    m_vidSize   = g_FbMemTotal - primary;
    m_pVidHeap  = new SurfaceHeap(m_vidSize, 0);
    CERF_LOG_X("cerf_guest: EnsureVideoHeap vidBasePa", m_vidBasePa);
    CERF_LOG_X("cerf_guest: EnsureVideoHeap vidSize",   m_vidSize);
    return m_pVidHeap != NULL;
}

void CerfDDGPE::GetVirtualVideoMemory(unsigned long* base, unsigned long* size,
                                      unsigned long* freeBytes) {
    EnsureVideoHeap();
    if (base)      *base      = (unsigned long)m_vidBaseVa;
    if (size)      *size      = m_vidSize;
    if (freeBytes) *freeBytes = m_pVidHeap ? m_pVidHeap->Available() : 0;
    CERF_LOG_X("cerf_guest: GetVirtualVideoMemory size", m_vidSize);
}

bool CerfDDGPE::SurfaceFbPa(GPESurf* s, ULONG* pa) {
    if (s == NULL) return false;
    if (s == m_pPrimarySurface) { *pa = CerfGpeFbMemBasePa(); return true; }
    if (s->InVideoMemory()) {
        *pa = m_vidBasePa + s->OffsetInVideoMemory();
        return true;
    }
    return false;
}

SCODE CerfDDGPE::BltComplete(GPEBltParms* p) {
    CERF_LOG_X_DEV("cerf_guest: GPE::BltComplete rop4", p ? p->rop4 : 0);
    return S_OK;
}

SCODE CerfDDGPE::Line(GPELineParms* pLineParms, EGPEPhase phase) {
    CERF_LOG_X_DEV("cerf_guest: GPE::Line phase", phase);
    if (phase == gpeSingle || phase == gpePrepare) {
        pLineParms->pLine = (SCODE (GPE::*)(GPELineParms*))&CerfDDGPE::HostLine;
    }
    return S_OK;
}

/* The DDGPE lib's AllocVideoSurface routes here with GPE_REQUIRE_VIDEO_MEMORY
   (ddgpe.cpp:85 -> AllocSurface(DDGPESurf**) -> (GPESurf**) cast, which drops
   pixelFormat — re-derived from format). REQUIRE/BACK_BUFFER must come from
   video memory; PREFER tries video then falls back; otherwise system memory. */
SCODE CerfDDGPE::AllocSurface(GPESurf** ppSurf, int width, int height,
                              EGPEFormat format, int surfaceFlags) {
    CERF_LOG_X_DEV("cerf_guest: AllocSurface w", (DWORD)width);
    CERF_LOG_X_DEV("cerf_guest: AllocSurface h", (DWORD)height);
    CERF_LOG_X_DEV("cerf_guest: AllocSurface fmt", (DWORD)format);
    CERF_LOG_X_DEV("cerf_guest: AllocSurface flags", (DWORD)surfaceFlags);

    const bool wantVideo =
        (surfaceFlags & (GPE_REQUIRE_VIDEO_MEMORY |
                         GPE_PREFER_VIDEO_MEMORY |
                         GPE_BACK_BUFFER)) != 0;
    const bool requireVideo =
        (surfaceFlags & (GPE_REQUIRE_VIDEO_MEMORY | GPE_BACK_BUFFER)) != 0;

    if (wantVideo && EnsureVideoHeap()) {
        const int bpp = CerfFormatBpp(format);
        const int stride = ((bpp * width + 31) >> 5) << 2;  /* ddgpesurf.cpp:66 */
        const DWORD bytes = (DWORD)stride * (DWORD)height;
        SurfaceHeap* node = m_pVidHeap->Alloc(bytes);
        if (node) {
            void* pBits = (BYTE*)m_vidBaseVa + node->Address();
            CerfVidSurf* s = new CerfVidSurf(width, height, pBits, stride,
                                             format, CerfFormatToDDGPE(format),
                                             node->Address(), node);
            if (s && s->Buffer()) {
                CERF_LOG_X_DEV("cerf_guest: AllocSurface video offset", node->Address());
                CERF_LOG_X_DEV("cerf_guest: AllocSurface video obj", (DWORD)(ULONG_PTR)s);
                *ppSurf = s;
                return S_OK;
            }
            if (s) delete s; else node->Free();
        }
        CERF_LOG_DEV("cerf_guest: AllocSurface video carve failed");
        if (requireVideo) { *ppSurf = NULL; return E_OUTOFMEMORY; }
    } else if (requireVideo) {
        CERF_LOG_DEV("cerf_guest: AllocSurface require-video but no heap");
        *ppSurf = NULL;
        return E_OUTOFMEMORY;
    }

    GPESurf* sys = new GPESurf(width, height, format);
    if (sys == NULL || sys->Buffer() == NULL) {
        if (sys) delete sys;
        *ppSurf = NULL;
        return E_OUTOFMEMORY;
    }
    CERF_LOG_DEV("cerf_guest: AllocSurface system memory");
    *ppSurf = sys;
    return S_OK;
}

/* Claim the hardware cursor and transport the shape to the host, which
   draws it as the host cursor (vmware model). Returning SPS_DECLINE would
   make the engine SW-draw it via DrvBitBlt, which doesn't survive the
   guest-additions host-blit path — the cursor vanishes. */
SCODE CerfDDGPE::SetPointerShape(GPESurf* pMask, GPESurf*, int xHot, int yHot,
                                 int cx, int cy) {
    if (pMask) CerfPublishCursor(pMask->Buffer(), pMask->Stride(),
                                 cx, cy, xHot, yHot, TRUE);
    else       CerfPublishCursor(NULL, 0, 0, 0, 0, 0, FALSE);
    return S_OK;
}

/* Position is tracked by the host pointer (absolute 1:1), so the host
   cursor is already where GWES thinks it is — nothing to do here. */
SCODE CerfDDGPE::MovePointer(int, int) { return S_OK; }

SCODE CerfDDGPE::SetPalette(const PALETTEENTRY* src, unsigned short firstEntry,
                            unsigned short numEntries) {
    CERF_LOG_X_DEV("cerf_guest: GPE::SetPalette numEntries", (DWORD)numEntries);
    if (src && firstEntry + numEntries <= 256) {
        for (unsigned short i = 0; i < numEntries; ++i) {
            m_palette[firstEntry + i] = src[i];
        }
        if (firstEntry + numEntries > m_paletteEntries) {
            m_paletteEntries = firstEntry + numEntries;
        }
    }
    return S_OK;
}

SCODE CerfDDGPE::GetPalette(PALETTEENTRY** ppPalette, unsigned short* pcEntries) {
    CERF_LOG_X_DEV("cerf_guest: GPE::GetPalette entries", (DWORD)m_paletteEntries);
    if (ppPalette) *ppPalette = (m_paletteEntries > 0) ? m_palette : NULL;
    if (pcEntries) *pcEntries = m_paletteEntries;
    return S_OK;
}

/* Single mode at the live g_Fb* dimensions. Runtime resize is driven by the
   rotation-escape path (DrvEscape), not by enumerating alternate modes. */
SCODE CerfDDGPE::GetModeInfo(GPEMode* pMode, int modeNo) {
    CERF_LOG_X_DEV("cerf_guest: GPE::GetModeInfo modeNo", (DWORD)modeNo);
    if (modeNo != 0 || pMode == NULL) return E_FAIL;
    pMode->modeId    = 0;
    pMode->width     = (int)g_FbWidth;
    pMode->height    = (int)g_FbHeight;
    pMode->Bpp       = (int)g_FbBpp;
    pMode->frequency = 60;
    pMode->format    = (g_FbBpp == 16) ? gpe16Bpp
                     : (g_FbBpp == 24) ? gpe24Bpp
                     : (g_FbBpp == 32) ? gpe32Bpp : gpe8Bpp;
    return S_OK;
}

int CerfDDGPE::NumModes() {
    CERF_LOG_DEV("cerf_guest: GPE::NumModes");
    return 1;
}

SCODE CerfDDGPE::ApplyFbMode() {
    void* fb = CerfMapFbMemory();
    if (!fb) {
        CERF_LOG("cerf_guest: GPE::ApplyFbMode FB map FAILED");
        return E_FAIL;
    }
    EGPEFormat fmt = (g_FbBpp == 16) ? gpe16Bpp
                   : (g_FbBpp == 24) ? gpe24Bpp
                   : (g_FbBpp == 32) ? gpe32Bpp : gpe8Bpp;

    if (m_pPrimarySurface) {
        delete m_pPrimarySurface;
        m_pPrimarySurface = NULL;
    }
    /* Primary MUST be a DDGPESurf, not a plain GPESurf: DDGPECreateSurface calls
       the DDGPESurf virtual SetDDGPESurf() on the primary for a
       DDSCAPS_PRIMARYSURFACE request (ddhsurf.cpp:204); a plain GPESurf has no
       such vtable and the virtual call faults. */
    m_pPrimarySurface = new DDGPESurf((int)g_FbWidth, (int)g_FbHeight, fb,
                                      (int)g_FbStride, fmt, CerfFormatToDDGPE(fmt));
    if (m_pPrimarySurface == NULL) return E_OUTOFMEMORY;

    m_gpeMode.width     = (int)g_FbWidth;
    m_gpeMode.height    = (int)g_FbHeight;
    m_gpeMode.Bpp       = (int)g_FbBpp;
    m_gpeMode.frequency = 60;
    m_gpeMode.format    = fmt;
    m_pMode = &m_gpeMode;
    m_nScreenWidth  = m_gpeMode.width;
    m_nScreenHeight = m_gpeMode.height;
    CERF_LOG_X("cerf_guest: GPE::ApplyFbMode primary surface", (DWORD)m_pPrimarySurface);
    return S_OK;
}

SCODE CerfDDGPE::SetMode(int modeId, HPALETTE* pPalette) {
    if (modeId != 0) return E_FAIL;
    CERF_LOG("cerf_guest: GPE::SetMode allocating primary");
    SCODE sc = ApplyFbMode();
    if (sc != S_OK) return sc;
    m_gpeMode.modeId = 0;

    if (pPalette) {
        if (g_FbBpp == 16) {
            *pPalette = EngCreatePalette(PAL_BITFIELDS, 0, NULL,
                                          0xF800u, 0x07E0u, 0x001Fu);
        } else if (g_FbBpp == 32 || g_FbBpp == 24) {
            *pPalette = EngCreatePalette(PAL_BITFIELDS, 0, NULL,
                                          0x00FF0000u, 0x0000FF00u, 0x000000FFu);
        } else {
            *pPalette = EngCreatePalette(PAL_RGB, 0, NULL, 0, 0, 0);
        }
        CERF_LOG_X("cerf_guest: GPE::SetMode palette handle", (DWORD)*pPalette);
        if (*pPalette == NULL) return E_OUTOFMEMORY;
    }

    CERF_LOG_X("cerf_guest: GPE::SetMode primary surface", (DWORD)m_pPrimarySurface);
    return S_OK;
}

int CerfDDGPE::InVBlank() {
    CERF_LOG_DEV("cerf_guest: GPE::InVBlank");
    return 0;
}

ULONG CerfDDGPE::GetGraphicsCaps() {
    CERF_LOG_DEV("cerf_guest: GPE::GetGraphicsCaps");
    return 0;
}

/* The CE5/WM5 kernel's only runtime-resize hook is gwes's rotation apply path,
   which re-queries this driver's GDIINFO and resizes the desktop to it (gwes.exe
   sub_16488; CE6 sub_C0164EDC). The surface stays axis-aligned at g_Fb*;
   orientation is tracked state consumed by GET. ABI: VGAFLAT::DrvEscape. */
ULONG CerfDDGPE::DrvEscape(SURFOBJ* pso, ULONG iEsc, ULONG cjIn, PVOID pvIn,
                           ULONG cjOut, PVOID pvOut) {
    if (iEsc == QUERYESCSUPPORT) {
        DWORD code = (pvIn && cjIn >= sizeof(DWORD)) ? *(DWORD*)pvIn : 0;
        if (code == DRVESC_GETSCREENROTATION || code == DRVESC_SETSCREENROTATION) {
            CERF_LOG_X_DEV("cerf_guest: DrvEscape QUERYESCSUPPORT rot", code);
            return 1;
        }
        return GPE::DrvEscape(pso, iEsc, cjIn, pvIn, cjOut, pvOut);
    }
    if (iEsc == DRVESC_GETSCREENROTATION) {
        /* gwes passes cjOut=0 with a valid 4-byte pvOut (&word_B9444) and reads the
           result back, like VGAFLAT — so the write is gated on pvOut only. */
        if (pvOut)
            *(int*)pvOut = ((DMDO_0 | DMDO_90 | DMDO_180 | DMDO_270) << 8)
                         | (m_currentRotation & 0xFF);
        CERF_LOG_X_DEV("cerf_guest: DrvEscape GETSCREENROTATION cjOut", (DWORD)cjOut);
        return DISP_CHANGE_SUCCESSFUL;
    }
    if (iEsc == DRVESC_SETSCREENROTATION) {
        /* Orientation arrives in cjIn (CE GPE escape ABI). No pixel rotation. */
        m_currentRotation = (int)cjIn;
        /* The rotation apply is the only surface re-enable point on the CE5 path, so
           rebuild the primary at g_Fb* (the pump set it before this CDS) — otherwise
           GDI keeps drawing at the boot stride while the host renders the new one,
           shearing the screen. */
        if (m_pPrimarySurface == NULL ||
            m_pPrimarySurface->Width()  != (int)g_FbWidth ||
            m_pPrimarySurface->Height() != (int)g_FbHeight)
            ApplyFbMode();
        CERF_LOG_X_DEV("cerf_guest: DrvEscape SETSCREENROTATION orient", (DWORD)cjIn);
        return DISP_CHANGE_SUCCESSFUL;
    }
    DWORD firstIn = (pvIn && cjIn >= sizeof(ULONG)) ? *(DWORD*)pvIn : 0xFFFFFFFFu;
    ULONG r = GPE::DrvEscape(pso, iEsc, cjIn, pvIn, cjOut, pvOut);
    CERF_LOG_X_DEV("cerf_guest: DrvEscape iEsc", (DWORD)iEsc);
    CERF_LOG_X_DEV("cerf_guest: DrvEscape firstIn", firstIn);
    CERF_LOG_X_DEV("cerf_guest: DrvEscape ret", (DWORD)r);
    return r;
}

BOOL CerfDDGPE::IsPaletteSettable() {
    CERF_LOG_DEV("cerf_guest: GPE::IsPaletteSettable");
    return (g_FbBpp <= 8);
}

BOOL CerfDDGPE::GetScreenDimensions(GPEScreenProps* pProps) {
    CERF_LOG_DEV("cerf_guest: GPE::GetScreenDimensions");
    if (!pProps) return FALSE;
    pProps->ulHorzSize   = (g_FbWidth  * 254) / 96 / 10;
    pProps->ulVertSize   = (g_FbHeight * 254) / 96 / 10;
    pProps->ulLogPixelsX = 96;
    pProps->ulLogPixelsY = 96;
    pProps->ulAspectX    = 36;
    pProps->ulAspectY    = 36;
    pProps->ulAspectXY   = 51;
    return TRUE;
}

ULONG* CerfDDGPE::GetClearTypeRGBMasks() {
    CERF_LOG_DEV("cerf_guest: GPE::GetClearTypeRGBMasks");
    return (g_FbBpp == 16) ? s_CerfRgbMasks_16bpp
         : (g_FbBpp == 32) ? s_CerfRgbMasks_32bpp : NULL;
}

extern "C" GPE* GetGPE() {
    static CerfDDGPE* s_instance = NULL;
    if (!s_instance) s_instance = new CerfDDGPE();
    return s_instance;
}

/* GetVirtualVideoMemory is CerfDDGPE-specific (not a GPE base virtual), so the
   DD-HAL in a separate TU reaches the offscreen extent through this shim. */
extern "C" void CerfGetVideoMem(unsigned long* base, unsigned long* size,
                                unsigned long* freeBytes) {
    ((CerfDDGPE*)GetGPE())->GetVirtualVideoMemory(base, size, freeBytes);
}

/* The SURFOBJ->surface resolution (FB-PA + masks) is CerfDDGPE-instance state,
   so a DDI function in a separate TU reaches it through this shim. */
extern "C" void CerfFillSurfaceFromSurfobj(CerfVirt::CerfBltSurface* s,
                                           SURFOBJ* pso) {
    ((CerfDDGPE*)GetGPE())->FillSurfaceFromSurfobj(s, pso);
}

/* Resolve a DirectDraw surface (DDRAWI_DDRAWSURFACE_LCL) to its FB physical
   address + geometry; the DD-HAL Lock aperture-maps the locked rect from this,
   since PA-only FB surfaces have no standing VA. FALSE for a system surface. */
extern "C" BOOL CerfDDSurfFbInfo(void* lcl, ULONG* pa, int* stride, int* bpp,
                                 int* height) {
    if (!lcl) return FALSE;
    DDGPESurf* s = DDGPESurf::GetDDGPESurf((LPDDRAWI_DDRAWSURFACE_LCL)lcl);
    if (!s) return FALSE;
    ULONG p;
    if (!((CerfDDGPE*)GetGPE())->SurfaceFbPa(s, &p)) return FALSE;
    if (pa)     *pa = p;
    if (stride) *stride = s->Stride();
    if (bpp)    *bpp = CerfFormatBpp(s->Format());
    if (height) *height = s->Height();
    return TRUE;
}
