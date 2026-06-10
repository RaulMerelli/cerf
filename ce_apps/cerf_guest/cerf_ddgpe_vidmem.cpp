#include "cerf_ddgpe.h"

#ifndef DMDO_0
#define DMDO_0 0
#endif

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

/* Flip-chain back buffer: CPU-writable guest RAM (DDGPESurf self-alloc) but tagged
   VIDEOMEMORY so DDGPECreateSurface won't OR in SYSTEMMEMORY — a video/system class
   mismatch makes ddcore reject the Flip (sub_377C594). SurfaceFbPa keys on FB-region
   residency, not this cap, so it still resolves to the guest-RAM VA. */
class CerfSysVidSurf : public DDGPESurf {
public:
    CerfSysVidSurf(int w, int h, EGPEFormat fmt) : DDGPESurf(w, h, fmt) {
        m_fInVideoMemory = 1;
    }
};

/* Bring up the offscreen video-memory heap over the CERF fb region tail past
   the primary. Uses only the fb-reg dims (present from DLL attach), so it is
   callable at HALInit time, before SetMode allocates the primary. */
bool CerfDDGPE::EnsureVideoHeap() {
    if (m_pVidHeap) return true;
    ULONG primary = g_FbPrimaryReserve ? g_FbPrimaryReserve
                                       : g_FbStride * g_FbHeight;
    if (g_FbMemTotal <= primary) {
        CERF_LOG_X("cerf_guest: EnsureVideoHeap no offscreen; memtotal", g_FbMemTotal);
        return false;
    }
    m_vidSize = g_FbMemTotal - primary;
    if (m_vidBacking == kCerfVidGlobalFb) {
        /* FCSE/shared-HAL: offscreen heap is the FB-PA aperture tail past the primary
           (PA as identity VA); surfaces resolve FB-PA, the host accelerator blits by PA. */
        void* base = CerfMapFbMemory();
        if (!base) {
            CERF_LOG("cerf_guest: EnsureVideoHeap FB map FAILED");
            return false;
        }
        m_vidBaseVa = (BYTE*)base + primary;
    } else {
        /* ASID: gwes VirtualAllocCopyEx rejects host-MMIO FB-PA (gwes.dll sub_C01B01EC
           -> DirectDrawCreate OOM), so the heap is guest RAM it maps into each client. */
        m_vidBaseVa = (BYTE*)VirtualAlloc(NULL, m_vidSize, MEM_RESERVE | MEM_COMMIT,
                                          PAGE_READWRITE);
        if (!m_vidBaseVa) {
            CERF_LOG_X("cerf_guest: EnsureVideoHeap VirtualAlloc FAILED gle", GetLastError());
            return false;
        }
    }
    m_pVidHeap  = new SurfaceHeap(m_vidSize, 0);
    return m_pVidHeap != NULL;
}

void CerfDDGPE::GetVirtualVideoMemory(unsigned long* base, unsigned long* size,
                                      unsigned long* freeBytes) {
    EnsureVideoHeap();
    if (base)      *base      = (unsigned long)m_vidBaseVa;
    if (size)      *size      = m_vidSize;
    if (freeBytes) *freeBytes = m_pVidHeap ? m_pVidHeap->Available() : 0;
}

bool CerfDDGPE::SurfaceFbPa(GPESurf* s, ULONG* pa) {
    if (s == NULL) return false;
    if (s == m_pPrimarySurface) { *pa = CerfGpeFbMemBasePa(); return true; }
    /* GlobalFb: offscreen video-heap surfaces are in the FB-PA aperture, so they
       resolve to their PA (host blits by PA; client Locks get the global VA). In
       GuestRamHeap mode the heap is guest RAM, so only the primary is FB-PA. */
    if (m_vidBacking == kCerfVidGlobalFb && s->InVideoMemory() && m_vidBaseVa) {
        BYTE* buf = (BYTE*)s->Buffer();
        if (buf >= m_vidBaseVa && buf < m_vidBaseVa + m_vidSize) {
            *pa = (ULONG)(ULONG_PTR)buf;   /* buf is the FB PA (identity) in GlobalFb */
            return true;
        }
    }
    return false;
}

/* Shadow MUST be carved from the vidmem heap (REQUIRE_VIDEO), not a plain alloc: only
   [VidMemBase,+vidSize] is mapped into the client by the ddraw runtime, so a shadow
   outside it faults the client Lock on CE6. */
DDGPESurf* CerfDDGPE::EnsurePrimaryShadow() {
    if (m_pPrimaryShadow) return m_pPrimaryShadow;
    if (!m_pPrimarySurface) return NULL;
    GPESurf* s = NULL;
    if (AllocSurface(&s, m_pPrimarySurface->Width(), m_pPrimarySurface->Height(),
                     m_pPrimarySurface->Format(), GPE_REQUIRE_VIDEO_MEMORY) == S_OK)
        m_pPrimaryShadow = (DDGPESurf*)s;
    return m_pPrimaryShadow;
}

/* Unlock-time present: copy the client-written shadow to the FB-PA scanout so the
   host renderer displays it. */
void CerfDDGPE::PrimaryShadowPresent() {
    DDGPESurf* primary = DDGPEPrimarySurface();
    if (!m_pPrimaryShadow || !primary) return;
    RECT r;
    r.left = 0; r.top = 0;
    r.right  = primary->Width();
    r.bottom = primary->Height();
    BltExpanded(primary, m_pPrimaryShadow, NULL, &r, &r, 0u, 0u, 0xCCCCu);
}

/* The DD-HAL Lock (separate TU) needs the shadow VA for the locked origin + the
   Unlock present; reach the CerfDDGPE-instance shadow through these shims. */
extern "C" unsigned long CerfPrimaryShadowLockVa(unsigned long origin_pa) {
    DDGPESurf* sh = ((CerfDDGPE*)GetGPE())->EnsurePrimaryShadow();
    if (!sh || !sh->Buffer()) return 0;
    return (unsigned long)(ULONG_PTR)sh->Buffer() + (origin_pa - CerfGpeFbMemBasePa());
}
extern "C" void CerfPrimaryShadowPresent(void) {
    ((CerfDDGPE*)GetGPE())->PrimaryShadowPresent();
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

    /* GPE_BACK_BUFFER is not a video-heap request: flip-chain back buffers are
       Lock'd + CPU-written, so they're allocated as VIDEOMEMORY-tagged guest RAM
       below, never from the host-MMIO FB-PA heap. */
    const bool wantVideo =
        (surfaceFlags & (GPE_REQUIRE_VIDEO_MEMORY |
                         GPE_PREFER_VIDEO_MEMORY)) != 0;
    const bool requireVideo =
        (surfaceFlags & GPE_REQUIRE_VIDEO_MEMORY) != 0;

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

    if (surfaceFlags & GPE_BACK_BUFFER) {
        CerfSysVidSurf* bb = new CerfSysVidSurf(width, height, format);
        if (bb == NULL || bb->Buffer() == NULL) {
            if (bb) delete bb;
            *ppSurf = NULL;
            return E_OUTOFMEMORY;
        }
        CERF_LOG_DEV("cerf_guest: AllocSurface back buffer (guest RAM, video-tagged)");
        *ppSurf = bb;
        return S_OK;
    }

    DDGPESurf* sys = new DDGPESurf(width, height, format);
    if (sys == NULL || sys->Buffer() == NULL) {
        if (sys) delete sys;
        *ppSurf = NULL;
        return E_OUTOFMEMORY;
    }
    CERF_LOG_DEV("cerf_guest: AllocSurface system memory");
    *ppSurf = sys;
    return S_OK;
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
    /* CerfVidSurf (a DDGPESurf): DDGPECreateSurface calls the virtual SetDDGPESurf on
       the primary (ddhsurf.cpp:204) — a plain GPESurf faults — and m_fInVideoMemory=1
       tags it VIDEOMEMORY (it IS the FB-PA scanout), so DDGPE won't OR in SYSTEMMEMORY
       and make ddcore reject the flip on a primary/back-buffer class mismatch. */
    m_pPrimarySurface = new CerfVidSurf((int)g_FbWidth, (int)g_FbHeight, fb,
                                        (int)g_FbStride, fmt, CerfFormatToDDGPE(fmt),
                                        0u, NULL);
    if (m_pPrimarySurface == NULL) return E_OUTOFMEMORY;
    /* SetRotation sets m_ScreenWidth/Height (the GPESurf ctor leaves them 0,
       GPE/gpe.cpp:419) that AllocBackBuffer sizes the backbuffer from. The
       cerf_guest surface is axis-aligned at g_Fb*; orientation is tracked-only
       state (DrvEscape), never applied to pixels, so the angle is DMDO_0. */
    m_pPrimarySurface->SetRotation((int)g_FbWidth, (int)g_FbHeight, DMDO_0);

    m_gpeMode.width     = (int)g_FbWidth;
    m_gpeMode.height    = (int)g_FbHeight;
    m_gpeMode.Bpp       = (int)g_FbBpp;
    m_gpeMode.frequency = 60;
    m_gpeMode.format    = fmt;
    m_pMode = &m_gpeMode;
    m_nScreenWidth  = m_gpeMode.width;
    m_nScreenHeight = m_gpeMode.height;
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
        if (*pPalette == NULL) return E_OUTOFMEMORY;
    }
    return S_OK;
}

/* The CE5 runtime/gemstone read the surface pixel buffer from lpGbl->fpVidMem, but
   the DDGPE lib only binds its DDGPESurf via lcl->dwReserved1 and leaves fpVidMem 0;
   expose the DDGPESurf's buffer VA so the CE5 HAL can populate fpVidMem itself. */
extern "C" unsigned long CerfDDGPESurfBufferVa(unsigned long surf) {
    return surf ? (unsigned long)(ULONG_PTR)((DDGPESurf*)(ULONG_PTR)surf)->Buffer() : 0;
}

/* GetVirtualVideoMemory is CerfDDGPE-specific (not a GPE base virtual), so the
   DD-HAL in a separate TU reaches the offscreen extent through this shim. */
extern "C" void CerfGetVideoMem(unsigned long* base, unsigned long* size,
                                unsigned long* freeBytes) {
    ((CerfDDGPE*)GetGPE())->GetVirtualVideoMemory(base, size, freeBytes);
}

/* Driver-construction-layer backing selection (called from DrvEnableDriver before any
   surface alloc): FCSE/shared-HAL kernels (CE<=5) need the cross-process global FB
   mapping; ASID kernels (CE6+) need the per-client guest-RAM heap + Lock-shadow. */
extern "C" void CerfSetVidBackingByOsMajor(unsigned long os_major) {
    ((CerfDDGPE*)GetGPE())->SetVidBacking(
        (os_major >= 6u) ? kCerfVidGuestRamHeap : kCerfVidGlobalFb);
}

extern "C" BOOL CerfVidBackingIsGlobalFb(void) {
    return ((CerfDDGPE*)GetGPE())->VidBacking() == kCerfVidGlobalFb;
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
