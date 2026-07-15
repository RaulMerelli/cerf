#include "cerf_ddgpe.h"
#include "cerf_dma_arena.h"

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

static void CerfSpan(int x0, int y0, int x1, int y1, int stride, int bits,
                     LONG* lo_off, ULONG* span) {
    const LONG row_lo = ((LONG)x0 * (LONG)bits) / 8;
    const LONG row_hi = (((LONG)x1 * (LONG)bits) + 7) / 8 - 1;
    const LONG first  = (LONG)y0       * (LONG)stride;
    const LONG last   = (LONG)(y1 - 1) * (LONG)stride;
    *lo_off = ((first < last) ? first : last) + row_lo;
    *span   = (ULONG)((((first < last) ? last : first) + row_hi) - *lo_off + 1);
}

ULONG CerfSpanBytes(int x0, int y0, int x1, int y1, int stride, int bits) {
    LONG lo; ULONG sp;
    CerfSpan(x0, y0, x1, y1, stride, bits, &lo, &sp);
    return sp;
}

static void CerfLineYExtent(const GPELineParms* p, int* ymin, int* ymax) {
    static const signed char kDirDy[8][2] = {
        { 0,  1 }, { 1,  0 }, { 1,  0 }, { 0,  1 },
        { 0, -1 }, { -1, 0 }, { -1, 0 }, { 0, -1 },
    };
    const int maj_dy = kDirDy[p->iDir & 7][0];
    const int min_dy = kDirDy[p->iDir & 7][1];
    long accum = (long)p->dN + p->llGamma;
    const long axstp = (long)p->dN;
    const long dgstp = (long)p->dN - (long)p->dM;
    int y = p->yStart, lo = y, hi = y, n;
    for (n = p->cPels; n > 0; --n) {
        if (y < lo) lo = y;
        if (y > hi) hi = y;
        if (n == 1) break;
        y += maj_dy;
        if (axstp) {
            if (accum < 0) accum += axstp;
            else { y += min_dy; accum += dgstp; }
        }
    }
    *ymin = lo; *ymax = hi;
}

static int CerfSrcDyAt(int dst_len, int src_len, int k) {
    int src_pos = 0, c;
    if (dst_len == src_len) return k;
    if (dst_len > src_len) {
        const int d_minor = 2 * src_len, d_major = 2 * src_len - 2 * dst_len;
        int accum = 3 * src_len - 2 * dst_len;
        for (c = 0; c < dst_len; ++c) {
            if (c == k) return src_pos;
            if (accum < 0) accum += d_minor;
            else { accum += d_major; ++src_pos; }
        }
    } else {
        const int d_minor = 2 * dst_len, d_major = 2 * dst_len - 2 * src_len;
        int accum = 2 * dst_len - src_len;
        while (accum < 0) { accum += d_minor; ++src_pos; }
        accum += d_major;
        for (c = 0; c < dst_len; ++c) {
            if (c == k) return src_pos;
            ++src_pos;
            while (accum < 0) { ++src_pos; accum += d_minor; }
            accum += d_major;
        }
    }
    return src_pos;
}

static void CerfStageSurface(CerfVirt::CerfBltSurface* s, ULONG buffer_va,
                             int x0, int y0, int x1, int y1,
                             int stride, int bits, CerfStageWb* wb) {
    LONG  lo_off;
    ULONG span;
    if (stride == 0 || bits <= 0 || x1 <= x0 || y1 <= y0) {
        CERF_LOG_X("cerf_guest: Stage BAD GEOMETRY x0", (ULONG)x0);
        CERF_LOG_X("cerf_guest: Stage BAD GEOMETRY y0", (ULONG)y0);
        CERF_LOG_X("cerf_guest: Stage BAD GEOMETRY x1", (ULONG)x1);
        CERF_LOG_X("cerf_guest: Stage BAD GEOMETRY y1", (ULONG)y1);
        CERF_LOG_X("cerf_guest: Stage BAD GEOMETRY stride", (ULONG)stride);
        CERF_LOG_X("cerf_guest: Stage BAD GEOMETRY bits", (ULONG)bits);
        CERF_FATAL("cerf_guest: Stage BAD GEOMETRY - halting");
    }

    CerfSpan(x0, y0, x1, y1, stride, bits, &lo_off, &span);

    ULONG arena_off = 0u;
    void* dstp = CerfArenaAlloc(span, &arena_off);
    memcpy(dstp, (const void*)(ULONG_PTR)((LONG)buffer_va + lo_off), span);

    s->buffer    = (uint32_t)((LONG)arena_off - lo_off);
    s->stage_off = arena_off;
    s->stage_len = span;
    s->is_fb_pa  = 0u;

    if (wb) {
        wb->active    = TRUE;
        wb->dst_va    = (ULONG)((LONG)buffer_va + lo_off);
        wb->arena_ptr = dstp;
        wb->span      = span;
    }
}

SCODE CerfDDGPE::BltPrepare(GPEBltParms* p) {
    ULONG pa;
    const bool dstHw = p->pDst && CerfConvertibleFmt(p->pDst->Format()) &&
                       (SurfaceFbPa(p->pDst, &pa) || p->pDst->Buffer() != NULL);
    const bool srcFb = p->pSrc && SurfaceFbPa(p->pSrc, &pa);
    if (!dstHw && !srcFb) {
        CERF_LOG_X("cerf_guest: BltPrepare no HW route, dst fmt",
                   (ULONG)(p->pDst ? p->pDst->Format() : 0));
        CERF_FATAL("cerf_guest: BltPrepare has no hardware route - halting");
    }
    p->pBlt = (SCODE (GPE::*)(GPEBltParms*))&CerfDDGPE::HwBlt;
    return S_OK;
}

void CerfDDGPE::RectToDesc(CerfVirt::CerfBltRect* r, const RECTL* s) {
    r->left = s->left; r->top = s->top; r->right = s->right; r->bottom = s->bottom;
}

void CerfDDGPE::FillSurface(CerfVirt::CerfBltSurface* s, GPESurf* surf,
                            int x0, int y0, int x1, int y1, bool host_writes,
                            bool read_palette, CerfStageWb* wb) {
    ULONG pa;
    (void)host_writes;
    s->format = (uint32_t)surf->Format();
    s->stride = (int32_t)surf->Stride();
    if (x1 < x0) { const int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { const int t = y0; y0 = y1; y1 = t; }
    if (SurfaceFbPa(surf, &pa)) { s->buffer = pa; s->is_fb_pa = 1u; }
    else {
        CerfStageSurface(s, (ULONG)(ULONG_PTR)surf->Buffer(),
                         x0, y0, x1, y1, surf->Stride(),
                         CerfFormatBpp(surf->Format()), wb);
    }
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

void CerfDDGPE::FillSurfaceFromSurfobj(CerfVirt::CerfBltSurface* s, SURFOBJ* pso,
                                      int y0, int y1, CerfStageWb* wb) {
    if (!pso) return;
    if (pso->dhsurf) {
        GPESurf* ds = (GPESurf*)pso->dhsurf;
        FillSurface(s, ds, 0, y0, ds->Width(), y1, true, true, wb);
        return;
    }
    s->format = (uint32_t)CerfIFormatToEGPE(pso->iBitmapFormat);
    s->stride = (int32_t)pso->lDelta;
    CerfStageSurface(s, (ULONG)(ULONG_PTR)pso->pvScan0,
                     0, y0, (int)pso->sizlBitmap.cx, y1,
                     (int)pso->lDelta, CerfFormatBpp((EGPEFormat)s->format), wb);
}

SCODE CerfDDGPE::HwBlt(GPEBltParms* p) {
    ULONG pa;
    if (!p->pDst || !p->prclDst || !CerfConvertibleFmt(p->pDst->Format())) {
        CERF_LOG_X("cerf_guest: HwBlt unsupported dst fmt",
                   (ULONG)(p->pDst ? p->pDst->Format() : 0));
        CERF_FATAL("cerf_guest: HwBlt dst has no hardware route - halting");
    }
    const bool dst_fb = SurfaceFbPa(p->pDst, &pa) ? true : false;
    if (!dst_fb && !p->pDst->Buffer())
        CERF_FATAL("cerf_guest: HwBlt dst has no buffer - halting");

    const bool has_src = (p->pSrc != NULL && p->prclSrc != NULL);
    bool src_fb = false, src_pal = false;
    if (has_src) {
        src_pal = (CerfFormatBpp(p->pSrc->Format()) <= 8) ? true : false;
        if (!src_pal && !CerfConvertibleFmt(p->pSrc->Format())) {
            CERF_LOG_X("cerf_guest: HwBlt unsupported src fmt", (ULONG)p->pSrc->Format());
            CERF_FATAL("cerf_guest: HwBlt src has no hardware route - halting");
        }
        src_fb = SurfaceFbPa(p->pSrc, &pa) ? true : false;
        if (!src_fb && !p->pSrc->Buffer())
            CERF_FATAL("cerf_guest: HwBlt src has no buffer - halting");
    }
    const bool has_mask  = (p->pMask != NULL && p->prclMask != NULL &&
                            p->pMask->Buffer() != NULL);
    const bool has_brush = (p->pBrush != NULL);

    int height = p->prclDst->bottom - p->prclDst->top;
    int width  = p->prclDst->right  - p->prclDst->left;
    if (height < 0) height = -height;
    if (width  < 0) width  = -width;
    if (height <= 0 || width <= 0) return S_OK;

    const int dl = p->prclDst->left, dt = p->prclDst->top, dr = p->prclDst->right;
    const int dst_stride = (int)p->pDst->Stride();
    const int dst_bits   = CerfFormatBpp(p->pDst->Format());

    int sl = 0, st = 0, sr = 0, src_stride = 0, src_bits = 0, src_w = 0, src_h = 0;
    if (has_src) {
        sl = p->prclSrc->left; st = p->prclSrc->top; sr = p->prclSrc->right;
        src_stride = (int)p->pSrc->Stride();
        src_bits   = CerfFormatBpp(p->pSrc->Format());
        src_w = p->prclSrc->right  - p->prclSrc->left; if (src_w < 0) src_w = -src_w;
        src_h = p->prclSrc->bottom - p->prclSrc->top;  if (src_h < 0) src_h = -src_h;
    }
    const bool stretch   = (p->bltFlags & 0x0008u) != 0u;
    const bool use_lut_y = has_src && stretch && (src_w != width || src_h != height);

    int ml = 0, mt = 0, mr = 0, mask_stride = 0, mask_bits = 0;
    if (has_mask) {
        ml = p->prclMask->left; mt = p->prclMask->top; mr = p->prclMask->right;
        mask_stride = (int)p->pMask->Stride();
        mask_bits   = CerfFormatBpp(p->pMask->Format());
    }

    bool overlap = false;
    if (has_src && !src_fb && !dst_fb &&
        (ULONG_PTR)p->pSrc->Buffer() == (ULONG_PTR)p->pDst->Buffer()) {
        const bool y_disjoint = p->prclSrc->bottom <= p->prclDst->top ||
                                p->prclDst->bottom <= p->prclSrc->top;
        const bool x_disjoint = p->prclSrc->right  <= p->prclDst->left ||
                                p->prclDst->right  <= p->prclSrc->left;
        overlap = !y_disjoint && !x_disjoint;
    }

    ULONG lut_bytes = 0;
    if (has_src && src_pal && p->pLookup)
        lut_bytes = (1u << CerfFormatBpp(p->pSrc->Format())) * (ULONG)sizeof(ULONG);
    int bw = 0, bh = 0;
    ULONG brush_span = 0;
    if (has_brush) {
        bw = p->pBrush->Width()  > 0 ? p->pBrush->Width()  : 1;
        bh = p->pBrush->Height() > 0 ? p->pBrush->Height() : 1;
        if (!SurfaceFbPa(p->pBrush, &pa))
            brush_span = CerfSpanBytes(0, 0, bw, bh, (int)p->pBrush->Stride(),
                                       CerfFormatBpp(p->pBrush->Format()));
    }
    const ULONG budget = CerfVirt::kDmaPartitionSize - CerfVirt::kDmaPartHdrSize
                         - (ULONG)sizeof(CerfVirt::CerfBltDescriptor)
                         - lut_bytes - brush_span - 64u;

    int r0 = 0;
    while (r0 < height) {
        int r1 = height;
        while (!overlap && r1 > r0 + 1) {
            ULONG t = 0;
            if (!dst_fb)
                t += CerfSpanBytes(dl, dt + r0, dr, dt + r1, dst_stride, dst_bits);
            if (has_src && !src_fb) {
                const int sy0 = use_lut_y ? CerfSrcDyAt(height, src_h, r0) : r0;
                const int sy1 = use_lut_y ? CerfSrcDyAt(height, src_h, r1 - 1) : (r1 - 1);
                t += CerfSpanBytes(sl, st + sy0, sr, st + sy1 + 1, src_stride, src_bits);
            }
            if (has_mask) {
                const int my0 = use_lut_y ? CerfSrcDyAt(height, src_h, r0) : r0;
                const int my1 = use_lut_y ? CerfSrcDyAt(height, src_h, r1 - 1) : (r1 - 1);
                t += CerfSpanBytes(ml, mt + my0, mr, mt + my1 + 1, mask_stride, mask_bits);
            }
            if (t <= budget) break;
            r1 = r0 + (r1 - r0) / 2;
        }

        if (!CerfArenaEnter()) CERF_FATAL("cerf_guest: DMA arena unavailable - halting");
        ULONG desc_off = 0u;
        CerfVirt::CerfBltDescriptor* pd = (CerfVirt::CerfBltDescriptor*)
            CerfArenaAlloc((ULONG)sizeof(CerfVirt::CerfBltDescriptor), &desc_off);
        if (!pd) CERF_FATAL("cerf_guest: DMA arena descriptor alloc failed - halting");
        CerfVirt::CerfBltDescriptor& d = *pd;
        memset(&d, 0, sizeof(d));
        d.magic          = CerfVirt::kCerfBltMagic;
        d.rop4           = (uint32_t)p->rop4;
        d.blt_flags      = (uint32_t)p->bltFlags;
        d.solid_color    = (uint32_t)p->solidColor;
        d.i_mode         = (uint32_t)p->iMode;
        d.x_positive     = p->xPositive ? 1u : 0u;
        d.y_positive     = p->yPositive ? 1u : 0u;
        d.blend_function = *(const ULONG*)&p->blendFunction;
        d.band_row_first = (uint32_t)r0;
        d.band_row_count = (uint32_t)(r1 - r0);

        CerfStageWb dstwb = {0};
        RectToDesc(&d.dst_rect, p->prclDst);
        FillSurface(&d.dst, p->pDst, dl, dt + r0, dr, dt + r1, true, true, &dstwb);

        if (has_src) {
            const int sy0 = use_lut_y ? CerfSrcDyAt(height, src_h, r0) : r0;
            const int sy1 = use_lut_y ? CerfSrcDyAt(height, src_h, r1 - 1) : (r1 - 1);
            d.has_src        = 1u;
            d.convert_active = (!src_pal && p->pConvert != NULL) ? 1u : 0u;
            if (src_pal && p->pLookup) {
                const ULONG entries = 1u << CerfFormatBpp(p->pSrc->Format());
                ULONG  lut_off = 0u;
                ULONG* lut = (ULONG*)CerfArenaAlloc(entries * (ULONG)sizeof(ULONG),
                                                    &lut_off);
                if (!lut) CERF_FATAL("cerf_guest: DMA arena lookup alloc failed - halting");
                memcpy(lut, p->pLookup, entries * sizeof(ULONG));
                d.lookup_off = lut_off;
                d.has_lookup = 1u;
            }
            d.to_mono = p->toMono ? 1u : 0u;
            d.mono_bg = (uint32_t)p->monoBg;
            RectToDesc(&d.src_rect, p->prclSrc);
            FillSurface(&d.src, p->pSrc, sl, st + sy0, sr, st + sy1 + 1, false);
        }
        if (has_mask) {
            const int my0 = use_lut_y ? CerfSrcDyAt(height, src_h, r0) : r0;
            const int my1 = use_lut_y ? CerfSrcDyAt(height, src_h, r1 - 1) : (r1 - 1);
            d.has_mask = 1u;
            RectToDesc(&d.mask_rect, p->prclMask);
            FillSurface(&d.mask, p->pMask, ml, mt + my0, mr, mt + my1 + 1, false);
        }
        if (has_brush) {
            d.has_brush    = 1u;
            d.brush_width  = (uint32_t)p->pBrush->Width();
            d.brush_height = (uint32_t)p->pBrush->Height();
            FillSurface(&d.brush, p->pBrush, 0, 0, bw, bh, false, false);
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

        const ULONG cgb = CerfGpeBlt(desc_off);
        if (cgb == 2u && dstwb.active)
            memcpy((void*)(ULONG_PTR)dstwb.dst_va, dstwb.arena_ptr, dstwb.span);
        CerfArenaLeave();
        if (cgb != 2u) CERF_FATAL("cerf_guest: host blit did not complete - halting");

        r0 = r1;
    }
    return S_OK;
}

extern "C" int CerfDDrawBlt(void* dstLcl, void* srcLcl, const RECTL* rDest,
                            const RECTL* rSrc, unsigned long ddFlags,
                            unsigned long ropArg, unsigned long fillColor,
                            unsigned long srcKeyOverride) {
    if (!dstLcl || !rDest) return 0;
    DDGPESurf* pDst = DDGPESurf::GetDDGPESurf((LPDDRAWI_DDRAWSURFACE_LCL)dstLcl);
    if (!pDst) return 0;
    DDGPESurf* pSrc = srcLcl
        ? DDGPESurf::GetDDGPESurf((LPDDRAWI_DDRAWSURFACE_LCL)srcLcl) : NULL;

    if (ddFlags & (0x2000u | 0x4000u)) return 0;

    ULONG solidColor = 0, rop4, bltFlags = 0;
    const RECT* prclSrc = NULL;
    if (ddFlags & 0x400u) {
        solidColor = fillColor;
        rop4 = 0xF0F0u;
        pSrc = NULL;
    } else {

        const ULONG ropByte = ((ddFlags & 0x20000u) && ropArg != 0u)
            ? ((ropArg >> 16) & 0xFFu) : 0xCCu;
        rop4 = (ropByte << 8) | ropByte;
        if (pSrc && rSrc) prclSrc = (const RECT*)rSrc;

        if ((ddFlags & 0x8000u) && pSrc) {
            bltFlags |= 4u;
            solidColor = pSrc->ColorKeyLow();
        } else if ((ddFlags & 0x10000u) && pSrc) {
            bltFlags |= 4u;
            solidColor = srcKeyOverride;
        }
    }
    CERF_LOG_X_DEV("cerf_guest: DDrawBlt ddFlags", ddFlags);
    CERF_LOG_X_DEV("cerf_guest: DDrawBlt ropArg", ropArg);
    CERF_LOG_X_DEV("cerf_guest: DDrawBlt rop4", rop4);
    SCODE sc = ((CerfDDGPE*)GetGPE())->BltExpanded(
        pDst, pSrc, NULL, (const RECT*)rDest, prclSrc, solidColor, bltFlags, (ROP4)rop4);
    return (sc == S_OK) ? 1 : 0;
}

SCODE CerfDDGPE::HostLine(GPELineParms* p) {
    if (!p || !p->pDst) CERF_FATAL("cerf_guest: HostLine has no dst - halting");
    ULONG pa;
    const EGPEFormat df = p->pDst->Format();
    if ((!SurfaceFbPa(p->pDst, &pa) && !p->pDst->Buffer()) ||
        (df != gpe1Bpp && df != gpe2Bpp && df != gpe4Bpp &&
         df != gpe8Bpp && df != gpe16Bpp && df != gpe24Bpp && df != gpe32Bpp)) {
        CERF_LOG_X("cerf_guest: HostLine unsupported dst fmt", (ULONG)df);
        CERF_FATAL("cerf_guest: HostLine dst has no hardware route - halting");
    }
    const bool dst_fb = SurfaceFbPa(p->pDst, &pa) ? true : false;

    int ymin = 0, ymax = -1;
    if (!dst_fb) {
        CerfLineYExtent(p, &ymin, &ymax);
        const int H = (int)p->pDst->Height();
        if (ymin < 0) ymin = 0;
        if (ymax > H - 1) ymax = H - 1;
        if (ymax < ymin) return S_OK;
    }

    const int   W          = (int)p->pDst->Width();
    const int   dst_stride = (int)p->pDst->Stride();
    const int   dst_bits   = CerfFormatBpp(df);
    const ULONG budget = CerfVirt::kDmaPartitionSize - CerfVirt::kDmaPartHdrSize
                         - (ULONG)sizeof(CerfVirt::CerfLineDescriptor) - 64u;

    int yb0 = ymin;
    for (;;) {
        int yb1 = dst_fb ? 0 : (ymax + 1);
        if (!dst_fb) {
            while (yb1 > yb0 + 1 &&
                   CerfSpanBytes(0, yb0, W, yb1, dst_stride, dst_bits) > budget)
                yb1 = yb0 + (yb1 - yb0) / 2;
        }

        if (!CerfArenaEnter()) CERF_FATAL("cerf_guest: DMA arena unavailable - halting");
        ULONG desc_off = 0u;
        CerfVirt::CerfLineDescriptor* pd = (CerfVirt::CerfLineDescriptor*)
            CerfArenaAlloc((ULONG)sizeof(CerfVirt::CerfLineDescriptor), &desc_off);
        if (!pd) CERF_FATAL("cerf_guest: DMA arena line alloc failed - halting");
        CerfVirt::CerfLineDescriptor& d = *pd;
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
        CerfStageWb dstwb = {0};
        if (dst_fb) {
            d.band_y_first = 0u;
            d.band_y_count = 0u;
            FillSurface(&d.dst, p->pDst, 0, 0, W, (int)p->pDst->Height(),
                        true, false, &dstwb);
        } else {
            d.band_y_first = (uint32_t)yb0;
            d.band_y_count = (uint32_t)(yb1 - yb0);
            FillSurface(&d.dst, p->pDst, 0, yb0, W, yb1, true, false, &dstwb);
        }
        const ULONG cgl = CerfGpeLine(desc_off);
        if (cgl == 2u && dstwb.active)
            memcpy((void*)(ULONG_PTR)dstwb.dst_va, dstwb.arena_ptr, dstwb.span);
        CerfArenaLeave();
        if (cgl != 2u) CERF_FATAL("cerf_guest: host line did not complete - halting");

        if (dst_fb) break;
        yb0 = yb1;
        if (yb0 > ymax) break;
    }
    return S_OK;
}
