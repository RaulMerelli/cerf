#pragma once

#include "cerf_virt_blt_descriptor.h"
#include "cerf_virt_blt_pixelops.h"
#include "cerf_virt_framebuffer.h"
#include "cerf_virt_addr_map.h"
#include "../../jit/arm_mmu.h"

#include <cstdint>

namespace CerfVirt {

/* Surface pixel addressing shared by the GPE host operations (blit, gradient,
   line). Non-Service base: each operation Service derives it and sets fb_/mmu_
   in OnReady, so the inner-loop accessors resolve guest-VA / FB-PA surfaces
   uniformly. */
class BltSurfaceAccess {
public:
    /* One surface resolved for an op: where its pixels live and how to address
       them. host_base is non-null only for an FB-region PA surface (contiguous
       host backing); a guest-VA surface resolves per-segment through the MMU. */
    struct Surface {
        const CerfBltSurface* desc;
        uint32_t bpp;
        bool     is_va;
        uint8_t* host_base;   /* FB-PA: host pointer to surface byte 0; VA: null */
    };

protected:
    bool ResolveSurface(const CerfBltSurface& s, uint32_t bpp, Surface* out);
    uint8_t* PixelPtr(const Surface& s, int32_t x, int32_t y, uint32_t run_bytes,
                      uint32_t* run);
    uint8_t* RotatedPixelPtr(const Surface& s, int32_t x, int32_t y);
    /* Read/write one bpp-byte pixel byte-by-byte when it straddles a 4KB page
       (guest VA is contiguous but the host pages backing it are not). */
    uint32_t ReadStraddlePixel(const Surface& s, int32_t x, int32_t y, uint32_t bpp);
    void     WriteStraddlePixel(const Surface& s, int32_t x, int32_t y,
                                uint32_t bpp, uint32_t value);

    CerfVirtFramebuffer* fb_ = nullptr;
    ArmMmu*              mmu_ = nullptr;
};

/* Channel masks for an RGB surface. The reference uses each surface's real
   m_pPalette masks (carried as pal_entries/mask), never an assumed set — some
   CE5 16bpp surfaces are BGR565 (R=0x001F,B=0xF800), not RGB565. Fall back to
   the per-format assumption only when the surface reports no real masks. */
inline bool ResolveMasks(const CerfBltSurface& s, uint32_t m[3], uint32_t* bpp) {
    const uint32_t bits = BltPixelOps::FormatBits(s.format);
    if (bits < 8u) return false;
    *bpp = bits / 8u;
    if (s.pal_entries >= 3u && (s.mask[0] | s.mask[1] | s.mask[2]) != 0u) {
        m[0] = s.mask[0]; m[1] = s.mask[1]; m[2] = s.mask[2];
        return true;
    }
    uint32_t ignore;
    return BltPixelOps::FormatMasks(s.format, m, &ignore);
}

inline bool BltSurfaceAccess::ResolveSurface(const CerfBltSurface& s, uint32_t bpp,
                                             Surface* out) {
    out->desc = &s;
    out->bpp  = bpp;
    out->is_va = (s.is_fb_pa == 0u);
    out->host_base = nullptr;
    if (!out->is_va) {
        const uint32_t fb_base = CerfVirt::kFramebufferMemBase;
        const uint32_t fb_size = fb_->RegionBytes();
        if (s.buffer < fb_base) return false;
        const uint32_t off = s.buffer - fb_base;
        if (off >= fb_size) return false;
        out->host_base = fb_->Bytes() + off;
    }
    return true;
}

/* Rotated surface: map logical (x,y) to the physical byte via the 4-angle
   transform (WINCE600 pixeliterator.cpp GetPtr). screen_w/h are physical dims
   minus 1 (0-indexed), matching ScreenWidth()-1 / ScreenHeight()-1 at Init. */
inline uint8_t* BltSurfaceAccess::RotatedPixelPtr(const Surface& s, int32_t x,
                                                  int32_t y) {
    const CerfBltSurface& d = *s.desc;
    const int32_t stride = d.stride;
    const int32_t bpp    = (int32_t)s.bpp;
    const int32_t W = (int32_t)d.screen_w - 1;
    const int32_t H = (int32_t)d.screen_h - 1;
    int32_t off;
    switch (d.rotate) {
    case kCerfRotate90:  off = (H - x) * stride + y * bpp;            break;
    case kCerfRotate180: off = (H - y) * stride + (W - x) * bpp;      break;
    case kCerfRotate270: off = x * stride + (W - y) * bpp;            break;
    default:             off = y * stride + x * bpp;                  break;
    }
    if (!s.is_va) return s.host_base + off;
    return mmu_->PeekVaToHost(d.buffer + (uint32_t)off);
}

/* Host pointer to pixel (x,y) plus the contiguous byte run available from there
   (run >= bpp on success). Rotated surfaces resolve one pixel at a time
   (addresses jump); linear FB-PA spans to region end; linear VA spans to page
   end. */
inline uint8_t* BltSurfaceAccess::PixelPtr(const Surface& s, int32_t x, int32_t y,
                                           uint32_t run_bytes, uint32_t* run) {
    if (s.desc->is_rotate) {
        uint8_t* p = RotatedPixelPtr(s, x, y);
        if (!p) return nullptr;
        *run = s.bpp;
        return p;
    }
    const uint32_t off = (uint32_t)y * (uint32_t)s.desc->stride + (uint32_t)x * s.bpp;
    if (!s.is_va) {
        const uint32_t fb_size = fb_->RegionBytes();
        const uint32_t base_off = (uint32_t)(s.host_base - fb_->Bytes());
        if (base_off + off >= fb_size) return nullptr;
        const uint32_t avail = fb_size - (base_off + off);
        *run = run_bytes < avail ? run_bytes : avail;
        return s.host_base + off;
    }
    const uint32_t va = s.desc->buffer + off;
    uint8_t* p = mmu_->PeekVaToHost(va);
    if (!p) return nullptr;
    const uint32_t page_left = 0x1000u - (va & 0x0FFFu);
    *run = run_bytes < page_left ? run_bytes : page_left;
    return p;
}

/* A 24bpp (3-byte) source pixel — or any multi-byte pixel — can straddle a 4KB
   page whose two halves map to non-adjacent host pages, so a single host read of
   bpp bytes from PixelPtr would cross into the wrong page. Assemble it byte-by-
   byte: the guest VA is contiguous, so each byte translates independently. */
inline uint32_t BltSurfaceAccess::ReadStraddlePixel(const Surface& s, int32_t x,
                                                    int32_t y, uint32_t bpp) {
    uint32_t v = 0;
    if (s.is_va) {
        const uint32_t va = s.desc->buffer
                          + (uint32_t)y * (uint32_t)s.desc->stride + (uint32_t)x * bpp;
        for (uint32_t i = 0; i < bpp; ++i) {
            uint8_t* bp = mmu_->PeekVaToHost(va + i);
            if (bp) v |= (uint32_t)(*bp) << (8u * i);
        }
    } else {
        const uint32_t off = (uint32_t)y * (uint32_t)s.desc->stride + (uint32_t)x * bpp;
        const uint32_t fb_size  = fb_->RegionBytes();
        const uint32_t base_off = (uint32_t)(s.host_base - fb_->Bytes());
        for (uint32_t i = 0; i < bpp; ++i) {
            if (base_off + off + i < fb_size)
                v |= (uint32_t)(s.host_base[off + i]) << (8u * i);
        }
    }
    return v;
}

inline void BltSurfaceAccess::WriteStraddlePixel(const Surface& s, int32_t x,
                                                 int32_t y, uint32_t bpp,
                                                 uint32_t value) {
    if (s.is_va) {
        const uint32_t va = s.desc->buffer
                          + (uint32_t)y * (uint32_t)s.desc->stride + (uint32_t)x * bpp;
        for (uint32_t i = 0; i < bpp; ++i) {
            uint8_t* bp = mmu_->PeekVaToHost(va + i);
            if (bp) *bp = (uint8_t)(value >> (8u * i));
        }
    } else {
        const uint32_t off = (uint32_t)y * (uint32_t)s.desc->stride + (uint32_t)x * bpp;
        const uint32_t fb_size  = fb_->RegionBytes();
        const uint32_t base_off = (uint32_t)(s.host_base - fb_->Bytes());
        for (uint32_t i = 0; i < bpp; ++i) {
            if (base_off + off + i < fb_size)
                s.host_base[off + i] = (uint8_t)(value >> (8u * i));
        }
    }
}

}  /* namespace CerfVirt */
