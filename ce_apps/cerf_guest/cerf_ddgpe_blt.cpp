#include "cerf_ddgpe.h"

SCODE CerfDDGPE::BltPrepare(GPEBltParms* p) {
    /* Route to the host when the dst is a 16/24/32bpp addressable surface, or
       whenever the src is FB-resident — an FB source must never reach the GPE
       CPU blit, which can't read its PA-only bytes. Everything else goes to
       SwBlt, which aperture-maps any FB surface before the CPU blit. */
    ULONG pa;
    const bool dstHw = p->pDst && p->prclDst && CerfConvertibleFmt(p->pDst->Format()) &&
                       (SurfaceFbPa(p->pDst, &pa) || p->pDst->Buffer() != NULL);
    const bool srcFb = p->pSrc && SurfaceFbPa(p->pSrc, &pa);
    if (dstHw || srcFb) {
        p->pBlt = (SCODE (GPE::*)(GPEBltParms*))&CerfDDGPE::HwBlt;
        return S_OK;
    }
    p->pBlt = (SCODE (GPE::*)(GPEBltParms*))&CerfDDGPE::SwBlt;
    return S_OK;
}

void CerfDDGPE::RectToDesc(CerfVirt::CerfBltRect* r, const RECTL* s) {
    r->left = s->left; r->top = s->top; r->right = s->right; r->bottom = s->bottom;
}

void CerfDDGPE::FillSurface(CerfVirt::CerfBltSurface* s, GPESurf* surf, bool read_palette) {
    ULONG pa;
    s->format = (uint32_t)surf->Format();
    s->stride = (int32_t)surf->Stride();
    if (SurfaceFbPa(surf, &pa)) { s->buffer = pa; s->is_fb_pa = 1u; }
    else { s->buffer = (uint32_t)(ULONG_PTR)surf->Buffer(); s->is_fb_pa = 0u; }
    /* A realized brush is raw memory cast to GPESurf* and only Init()'d, so its
       m_Format is never constructed — uninitialized garbage (GPE/ddi_if.cpp
       DrvRealizeBrush, GPE/gpe.cpp GPESurf::Init). Reading m_pPalette off a brush
       dereferences that garbage and faults; brush callers pass read_palette=false. */
    GPEFormat* gf = read_palette ? surf->FormatPtr() : NULL;
    s->pal_entries = gf ? (uint32_t)gf->m_PaletteEntries : 0u;
    if (gf && gf->m_pPalette && (gf->m_PaletteEntries == 3 || gf->m_PaletteEntries == 4)) {
        for (int i = 0; i < gf->m_PaletteEntries; ++i) s->mask[i] = gf->m_pPalette[i];
    }
    if (surf->IsRotate()) {
        s->is_rotate = 1u;
        switch (surf->Rotate()) {
            case DMDO_90:  s->rotate = CerfVirt::kCerfRotate90;  break;
            case DMDO_180: s->rotate = CerfVirt::kCerfRotate180; break;
            case DMDO_270: s->rotate = CerfVirt::kCerfRotate270; break;
            default:       s->rotate = CerfVirt::kCerfRotate0;   break;
        }
        s->screen_w = (uint32_t)surf->ScreenWidth();
        s->screen_h = (uint32_t)surf->ScreenHeight();
    }
}

/* Resolve a SURFOBJ to a host surface the same way the GPE lib does
   (TmpGPESurf): a device-managed surface carries its GPESurf in dhsurf;
   an engine bitmap exposes its bits directly. Used by DrvGradientFill,
   which receives a raw SURFOBJ rather than a GPESurf. */
void CerfDDGPE::FillSurfaceFromSurfobj(CerfVirt::CerfBltSurface* s, SURFOBJ* pso) {
    if (!pso) return;
    if (pso->dhsurf) { FillSurface(s, (GPESurf*)pso->dhsurf); return; }
    /* gpe.h IFormatToEGPEFormat[] maps BMF_16/24/32 (4/5/6) to gpe16/24/32Bpp
       (4/5/6) — identity for the formats the host renders; lower BMF values
       differ but the host declines them (ResolveMasks fails). */
    s->format   = (uint32_t)pso->iBitmapFormat;
    s->stride   = (int32_t)pso->lDelta;
    s->buffer   = (uint32_t)(ULONG_PTR)pso->pvScan0;
    s->is_fb_pa = 0u;
}

/* SW fallback when the host engine cannot handle a blit (non-convertible dst,
   palettized src without lookup, untranslatable page); SwBlt aperture-maps any
   FB-resident surface so the GPE CPU blit addresses real bytes. */
SCODE CerfDDGPE::SwFallback(GPEBltParms* p) { return SwBlt(p); }

/* Host reads guest memory via PeekVaToHost, which cannot fault a page in, so
   an ODO demand-pager-recycled source page (L2=0, no TLB) reads unmapped and
   the host blit is dropped — icon color/mask lost. Touch one byte per
   spanned 4KB page in-guest first to fault it resident via the normal pager. */
void CerfDDGPE::FaultResident(GPESurf* surf, int x0, int y0, int x1, int y1) {
    ULONG pa;
    if (!surf || SurfaceFbPa(surf, &pa)) return;
    BYTE* base = (BYTE*)surf->Buffer();
    if (!base) return;
    /* Clamp to the surface extent before the raw deref below: the host reader
       takes off-surface coords through PeekVaToHost (graceful null), but this
       touches guest memory directly and would fault on a rect past the alloc. */
    const int W = surf->Width(), H = surf->Height();
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x1 <= x0 || y1 <= y0) return;
    const int stride = surf->Stride();
    const int bpp    = CerfFormatBpp(surf->Format());
    const int b0 = (x0 * bpp) / 8;
    const int b1 = (x1 * bpp + 7) / 8;
    volatile BYTE sink = 0;
    for (int y = y0; y < y1; ++y) {
        BYTE* row = base + (INT_PTR)y * stride;
        for (int b = b0; b < b1; b += 0x1000) sink ^= row[b];
        sink ^= row[b1 - 1];
    }
    (void)sink;
}

/* Build a full blit descriptor from GPEBltParms and run it on the host. The
   host covers every rop / stretch / mask / brush / alpha case; on an
   untranslatable page or a format it does not handle it returns non-done
   and we fall back to the GPE software blit. */
SCODE CerfDDGPE::HwBlt(GPEBltParms* p) {
    if (!p->pDst || !p->prclDst || !CerfConvertibleFmt(p->pDst->Format()))
        return SwFallback(p);
    ULONG pa;
    if (!SurfaceFbPa(p->pDst, &pa) && !p->pDst->Buffer()) return SwFallback(p);

    CerfVirt::CerfBltDescriptor d;
    memset(&d, 0, sizeof(d));
    d.magic          = CerfVirt::kCerfBltMagic;
    d.rop4           = (uint32_t)p->rop4;
    d.blt_flags      = (uint32_t)p->bltFlags;
    d.solid_color    = (uint32_t)p->solidColor;
    d.i_mode         = (uint32_t)p->iMode;
    d.x_positive     = p->xPositive ? 1u : 0u;
    d.y_positive     = p->yPositive ? 1u : 0u;
    d.blend_function = *(const ULONG*)&p->blendFunction;
    RectToDesc(&d.dst_rect, p->prclDst);
    FillSurface(&d.dst, p->pDst);

    if (p->pSrc && p->prclSrc) {
        const bool pal = CerfFormatBpp(p->pSrc->Format()) <= 8;  /* palettized index */
        if (pal) { if (!p->pLookup) return SwFallback(p); }
        else if (!CerfConvertibleFmt(p->pSrc->Format())) return SwFallback(p);
        if (!SurfaceFbPa(p->pSrc, &pa) && !p->pSrc->Buffer()) return SwFallback(p);
        d.has_src        = 1u;
        d.convert_active = (!pal && p->pConvert != NULL) ? 1u : 0u;
        d.lookup_va      = pal ? (uint32_t)(ULONG_PTR)p->pLookup : 0u;
        RectToDesc(&d.src_rect, p->prclSrc);
        FillSurface(&d.src, p->pSrc);
    }
    if (p->pMask && p->prclMask && p->pMask->Buffer()) {
        d.has_mask = 1u;
        RectToDesc(&d.mask_rect, p->prclMask);
        FillSurface(&d.mask, p->pMask);
    }
    if (p->pBrush) {
        d.has_brush    = 1u;
        d.brush_width  = (uint32_t)p->pBrush->Width();
        d.brush_height = (uint32_t)p->pBrush->Height();
        FillSurface(&d.brush, p->pBrush, false);
        if (p->pptlBrush) {
            d.brush_has_ptl = 1u;
            d.brush_ptl_x   = p->pptlBrush->x;
            d.brush_ptl_y   = p->pptlBrush->y;
        }
    }
    if (p->prclClip) {
        d.has_clip = 1u;
        RectToDesc(&d.clip_rect, p->prclClip);
    }

    if (p->prclDst)
        FaultResident(p->pDst, p->prclDst->left, p->prclDst->top,
                      p->prclDst->right, p->prclDst->bottom);
    if (p->pSrc && p->prclSrc)
        FaultResident(p->pSrc, p->prclSrc->left, p->prclSrc->top,
                      p->prclSrc->right, p->prclSrc->bottom);
    if (p->pMask && p->prclMask)
        FaultResident(p->pMask, p->prclMask->left, p->prclMask->top,
                      p->prclMask->right, p->prclMask->bottom);
    if (p->pBrush)
        FaultResident(p->pBrush, 0, 0, p->pBrush->Width(), p->pBrush->Height());

    const ULONG cgb = CerfGpeBlt((ULONG)(ULONG_PTR)&d);
    return (cgb == 2u) ? S_OK : SwFallback(p);
}

/* FB-resident dst (primary / video memory) → host line accelerator, addressed by
   PA; a system-memory dst uses EmulatedLine (CPU-drawn, small, mapped). */
SCODE CerfDDGPE::HostLine(GPELineParms* p) {
    ULONG pa;
    if (!p || !p->pDst || !SurfaceFbPa(p->pDst, &pa)) return GPE::EmulatedLine(p);
    CerfVirt::CerfLineDescriptor d;
    memset(&d, 0, sizeof(d));
    d.magic       = CerfVirt::kCerfLineMagic;
    d.x_start     = p->xStart;
    d.y_start     = p->yStart;
    d.c_pels      = p->cPels;
    d.d_m         = p->dM;
    d.d_n         = p->dN;
    d.ll_gamma    = p->llGamma;
    d.i_dir       = p->iDir;
    d.style       = p->style;
    d.style_state = p->styleState;
    d.solid_color = (uint32_t)p->solidColor;
    d.mix         = p->mix;
    FillSurface(&d.dst, p->pDst, false);
    CerfGpeLine((ULONG)(ULONG_PTR)&d);
    return S_OK;
}

/* Aperture-run a software blit on PA-only FB surfaces: map each FB surface's
   touched rows and aim a temp GPESurf at the window. pBits = window - top*stride
   so EmulatedBlt's coordinate (x, top) lands at the window's first row. */
SCODE CerfDDGPE::SwBlt(GPEBltParms* p) {
    GPESurf*     save[4] = { p->pDst, p->pSrc, p->pMask, p->pBrush };
    GPESurf**    slot[4] = { &p->pDst, &p->pSrc, &p->pMask, &p->pBrush };
    const RECTL* rect[4] = { p->prclDst, p->prclSrc, p->prclMask, 0 };
    GPESurf*     tmp[4]  = { 0, 0, 0, 0 };
    void*        win[4]  = { 0, 0, 0, 0 };
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        GPESurf* s = save[i];
        ULONG pa;
        if (!s || !SurfaceFbPa(s, &pa)) continue;
        const int stride = s->Stride();
        int top = 0, h = s->Height();
        if (rect[i]) {
            int t = rect[i]->top, b = rect[i]->bottom;
            if (t > b) { int sw = t; t = b; b = sw; }
            if (b > t) { top = t; h = b - t; }
        }
        win[i] = CerfMapFbWindow(pa + (ULONG)top * (ULONG)stride,
                                 (ULONG)h * (ULONG)stride);
        if (!win[i]) { ok = false; break; }
        void* pBits = (BYTE*)win[i] - (INT_PTR)top * stride;
        tmp[i] = new GPESurf(s->Width(), s->Height(), pBits, stride, s->Format());
        if (!tmp[i]) { ok = false; break; }
        *slot[i] = tmp[i];
    }
    SCODE rc = ok ? GPE::EmulatedBlt(p) : E_FAIL;
    for (int i = 0; i < 4; ++i) {
        *slot[i] = save[i];
        if (tmp[i]) delete tmp[i];
        if (win[i]) CerfUnmapFbWindow(win[i]);
    }
    return rc;
}
