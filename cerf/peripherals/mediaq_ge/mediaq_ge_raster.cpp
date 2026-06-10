#include "mediaq_ge.h"

#include "../../core/log.h"

#include <cstring>

/* Flat rectangle fill: pattern and source are both the fill colour, so each
   pixel is Rop3(rop, color, color, dest) (MQ-200 Data Book Table 5-63 solid
   pattern/source). */
void MediaQGe::FillSolid(uint8_t rop, uint32_t color, uint32_t cmd) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = DestStrideBytes();
    const uint32_t base   = BaseAddr();
    if (bpp == 0u || stride == 0u) {
        LOG(Caution, "MediaQ GE: solid fill unsupported depth/stride (GE0AR=0x%08X)\n",
            reg_[kGe0ADstStride]);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const uint32_t size = reg_[kGe01Size];
    const uint32_t w = size & 0xFFFu;          /* GE01R width[11:0]. */
    const uint32_t h = (size >> 16) & 0xFFFu;  /* GE01R height[27:16]. */
    const uint32_t xy = reg_[kGe02DstXY];
    const uint32_t x = xy & 0xFFFu;            /* GE02R dest X[11:0]. */
    const uint32_t y = (xy >> 16) & 0xFFFu;    /* GE02R dest Y[27:16]. */

    const bool clip = (cmd & kCmdEnClip) != 0u;
    const uint32_t cl = reg_[kGe05ClipLT] & 0xFFFu;
    const uint32_t ct = (reg_[kGe05ClipLT] >> 16) & 0xFFFu;
    const uint32_t cr = reg_[kGe06ClipRB] & 0xFFFu;
    const uint32_t cb = (reg_[kGe06ClipRB] >> 16) & 0xFFFu;

    uint8_t* const fb     = Fb();
    const uint32_t fbsize = FbBytes();
    const uint32_t pmask  = (bpp >= 4u) ? 0xFFFFFFFFu : ((1u << (bpp * 8u)) - 1u);

    for (uint32_t row = 0; row < h; ++row) {
        const uint32_t yy = y + row;
        if (clip && (yy < ct || yy > cb)) continue;
        for (uint32_t col = 0; col < w; ++col) {
            const uint32_t xx = x + col;
            if (clip && (xx < cl || xx > cr)) continue;
            const uint64_t addr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(yy) * stride + static_cast<uint64_t>(xx) * bpp;
            if (addr + bpp > fbsize) continue;
            uint32_t d = 0u;
            std::memcpy(&d, fb + addr, bpp);
            const uint32_t res = Rop3(rop, color, color, d) & pmask;
            std::memcpy(fb + addr, &res, bpp);
        }
    }
}

/* Screen-to-screen colour BitBLT (srcSys=0, monoSrc=0): src and dst rectangles
   of one image (single base GE0BR, shared stride GE0AR); GE00R[11]/[12]
   direction makes overlapping moves read-before-overwrite. */
void MediaQGe::BlitColorFromDisplay(uint32_t cmd) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = DestStrideBytes();
    const uint32_t base   = BaseAddr();
    if (bpp == 0u || stride == 0u) {
        LOG(Caution, "MediaQ GE: screen-to-screen blit unsupported depth/stride "
                     "(GE0AR=0x%08X)\n", reg_[kGe0ADstStride]);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint32_t size = reg_[kGe01Size];
    const uint32_t w = size & 0xFFFu;
    const uint32_t h = (size >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return;

    const int sx = static_cast<int>(reg_[kGe03SrcXY] & 0xFFFu);
    const int sy = static_cast<int>((reg_[kGe03SrcXY] >> 16) & 0xFFFu);
    const int dx = static_cast<int>(reg_[kGe02DstXY] & 0xFFFu);
    const int dy = static_cast<int>((reg_[kGe02DstXY] >> 16) & 0xFFFu);
    const int xstep = (cmd & kCmdXNeg) ? -1 : 1;
    const int ystep = (cmd & kCmdYNeg) ? -1 : 1;
    const uint8_t rop = static_cast<uint8_t>(cmd & kCmdRopMask);
    const bool trans  = (cmd & kCmdColorTrans) != 0u;
    const uint32_t key = reg_[kGe04ColorCmp] & ((bpp >= 4u) ? 0xFFFFFFFFu : ((1u << (bpp * 8u)) - 1u));
    const bool clip = (cmd & kCmdEnClip) != 0u;
    const int cl = static_cast<int>(reg_[kGe05ClipLT] & 0xFFFu);
    const int ct = static_cast<int>((reg_[kGe05ClipLT] >> 16) & 0xFFFu);
    const int cr = static_cast<int>(reg_[kGe06ClipRB] & 0xFFFu);
    const int cb = static_cast<int>((reg_[kGe06ClipRB] >> 16) & 0xFFFu);

    uint8_t* const fb     = Fb();
    const uint32_t fbsize = FbBytes();
    const uint32_t pmask  = (bpp >= 4u) ? 0xFFFFFFFFu : ((1u << (bpp * 8u)) - 1u);

    for (uint32_t r = 0; r < h; ++r) {
        const int syr = sy + ystep * static_cast<int>(r);
        const int dyr = dy + ystep * static_cast<int>(r);
        if (syr < 0 || dyr < 0) continue;
        if (clip && (dyr < ct || dyr > cb)) continue;
        for (uint32_t c = 0; c < w; ++c) {
            const int sxc = sx + xstep * static_cast<int>(c);
            const int dxc = dx + xstep * static_cast<int>(c);
            if (sxc < 0 || dxc < 0) continue;
            if (clip && (dxc < cl || dxc > cr)) continue;
            const uint64_t saddr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(syr) * stride + static_cast<uint64_t>(sxc) * bpp;
            const uint64_t daddr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(dyr) * stride + static_cast<uint64_t>(dxc) * bpp;
            if (saddr + bpp > fbsize || daddr + bpp > fbsize) continue;
            uint32_t s = 0u;
            std::memcpy(&s, fb + saddr, bpp);
            if (trans && (s & pmask) == key) continue;
            uint32_t d = 0u;
            std::memcpy(&d, fb + daddr, bpp);
            const uint32_t res = Rop3(rop, 0u, s, d) & pmask;
            std::memcpy(fb + daddr, &res, bpp);
        }
    }
}

/* Mono-source BitBLT with the 1-bpp source in display memory (srcSys=0,
   monoSrc=1): source bit at GE0BR + srcY*GE09R[9:0] stride, bit
   (GE09R[27:25]+srcX) MSB-first; set -> fg, clear -> bg, under the ROP and
   optional mono transparency (MQ-200 Data Book Table 5-63/5-72). */
void MediaQGe::BlitMonoFromDisplay(uint32_t cmd) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = DestStrideBytes();
    const uint32_t base   = BaseAddr();
    if (bpp == 0u || stride == 0u) {
        LOG(Caution, "MediaQ GE: mono-from-display blit unsupported depth/stride "
                     "(GE0AR=0x%08X)\n", reg_[kGe0ADstStride]);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint32_t size = reg_[kGe01Size];
    const uint32_t w = size & 0xFFFu;
    const uint32_t h = (size >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return;

    const int sx = static_cast<int>(reg_[kGe03SrcXY] & 0xFFFu);
    const int sy = static_cast<int>((reg_[kGe03SrcXY] >> 16) & 0xFFFu);
    const int dx = static_cast<int>(reg_[kGe02DstXY] & 0xFFFu);
    const int dy = static_cast<int>((reg_[kGe02DstXY] >> 16) & 0xFFFu);
    const int xstep = (cmd & kCmdXNeg) ? -1 : 1;
    const int ystep = (cmd & kCmdYNeg) ? -1 : 1;
    const uint32_t g09 = reg_[Lyt().src_stride_idx];
    const uint32_t src_stride = g09 & 0x3FFu;          /* GE09R[9:0] bytes. */
    const uint32_t bit_off0   = (g09 >> 25) & 0x7u;    /* GE09R[27:25]. */
    const uint8_t rop = static_cast<uint8_t>(cmd & kCmdRopMask);
    const uint32_t pmask = (bpp >= 4u) ? 0xFFFFFFFFu : ((1u << (bpp * 8u)) - 1u);
    const uint32_t fg = reg_[Lyt().fg_index] & pmask;
    const uint32_t bg = reg_[Lyt().bg_index] & pmask;
    const bool mono_trans  = (cmd & kCmdMonoTrans) != 0u;
    const bool trans_clear = mono_trans && (cmd & kCmdMonoTrPol) == 0u;
    const bool trans_set   = mono_trans && (cmd & kCmdMonoTrPol) != 0u;
    const bool clip = (cmd & kCmdEnClip) != 0u;
    const int cl = static_cast<int>(reg_[kGe05ClipLT] & 0xFFFu);
    const int ct = static_cast<int>((reg_[kGe05ClipLT] >> 16) & 0xFFFu);
    const int cr = static_cast<int>(reg_[kGe06ClipRB] & 0xFFFu);
    const int cb = static_cast<int>((reg_[kGe06ClipRB] >> 16) & 0xFFFu);

    uint8_t* const fb     = Fb();
    const uint32_t fbsize = FbBytes();

    for (uint32_t r = 0; r < h; ++r) {
        const int syr = sy + ystep * static_cast<int>(r);
        const int dyr = dy + ystep * static_cast<int>(r);
        if (syr < 0 || dyr < 0) continue;
        if (clip && (dyr < ct || dyr > cb)) continue;
        const uint64_t row_byte = static_cast<uint64_t>(base) +
            static_cast<uint64_t>(syr) * src_stride;
        for (uint32_t c = 0; c < w; ++c) {
            const int sxc = sx + xstep * static_cast<int>(c);
            const int dxc = dx + xstep * static_cast<int>(c);
            if (sxc < 0 || dxc < 0) continue;
            if (clip && (dxc < cl || dxc > cr)) continue;
            const uint32_t bit_idx = bit_off0 + static_cast<uint32_t>(sxc);
            const uint64_t sbyte = row_byte + (bit_idx >> 3);
            if (sbyte >= fbsize) continue;
            const uint32_t bit = (fb[sbyte] >> (7u - (bit_idx & 7u))) & 1u;
            if (bit && trans_set) continue;
            if (!bit && trans_clear) continue;
            const uint32_t color = bit ? fg : bg;
            const uint64_t daddr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(dyr) * stride + static_cast<uint64_t>(dxc) * bpp;
            if (daddr + bpp > fbsize) continue;
            uint32_t d = 0u;
            std::memcpy(&d, fb + daddr, bpp);
            const uint32_t res = Rop3(rop, 0u, color, d) & pmask;
            std::memcpy(fb + daddr, &res, bpp);
        }
    }
}

/* Bresenham hardware line draw (GE00R type[10:8]=100; MQ-200 Data Book
   Table 5-64..5-66 field decode cited inline). */
void MediaQGe::DrawLine(uint32_t cmd) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = DestStrideBytes();
    const uint32_t base   = BaseAddr();
    if (bpp == 0u || stride == 0u) {
        LOG(Caution, "MediaQ GE: line draw unsupported depth/stride (GE0AR=0x%08X)\n",
            reg_[kGe0ADstStride]);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const uint32_t pmask = (bpp >= 4u) ? 0xFFFFFFFFu : ((1u << (bpp * 8u)) - 1u);
    const uint32_t color = LineColor(cmd) & pmask;

    /* GE00R[25] ROP2: the ROP byte is the low nibble duplicated into [7:4]. */
    uint8_t rop = static_cast<uint8_t>(cmd & kCmdRopMask);
    if (cmd & kCmdRop2)
        rop = static_cast<uint8_t>((cmd & 0x0Fu) | ((cmd & 0x0Fu) << 4));

    int x = static_cast<int>(reg_[kGe02DstXY] & 0xFFFu);            /* GE02R[11:0]. */
    int y = static_cast<int>(reg_[kGe03SrcXY] & 0xFFFu);            /* GE03R[11:0]. */
    const int dmaj = static_cast<int>((reg_[kGe01Size] >> 17) & 0xFFFu);     /* GE01R[28:17]. */
    const int dmin = static_cast<int>((reg_[kGe03SrcXY] >> 12) & 0x1FFFFu);  /* GE03R[28:12]. */
    const bool no_last = (reg_[kGe01Size] & (1u << 30)) != 0u;      /* GE01R[30]. */

    bool y_major;
    int  step_x, step_y;
    if ((reg_[kGe01Size] & (1u << 31)) == 0u) {                     /* GE01R[31]=0: quadrant mode. */
        const uint32_t q = (reg_[kGe02DstXY] >> 29) & 0x7u;         /* GE02R[31:29]. */
        y_major = (q & 1u) != 0u;
        step_x  = (q & 2u) ? -1 : 1;
        step_y  = (q & 4u) ? -1 : 1;
    } else {                                                        /* GE01R[31]=1: direction-bit mode. */
        y_major = (reg_[kGe01Size] & (1u << 29)) != 0u;            /* GE01R[29]. */
        step_x  = (cmd & kCmdXNeg) ? -1 : 1;
        step_y  = (cmd & kCmdYNeg) ? -1 : 1;
    }

    const bool clip = (cmd & kCmdEnClip) != 0u;
    const int  cl = static_cast<int>(reg_[kGe05ClipLT] & 0xFFFu);
    const int  ct = static_cast<int>((reg_[kGe05ClipLT] >> 16) & 0xFFFu);
    const int  cr = static_cast<int>(reg_[kGe06ClipRB] & 0xFFFu);
    const int  cb = static_cast<int>((reg_[kGe06ClipRB] >> 16) & 0xFFFu);

    uint8_t* const fb     = Fb();
    const uint32_t fbsize = FbBytes();

    const int count = dmaj + (no_last ? 0 : 1);
    int err = dmaj >> 1;
    for (int i = 0; i < count; ++i) {
        if (x >= 0 && y >= 0 && !(clip && (x < cl || x > cr || y < ct || y > cb))) {
            const uint64_t addr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(y) * stride + static_cast<uint64_t>(x) * bpp;
            if (addr + bpp <= fbsize) {
                uint32_t d = 0u;
                std::memcpy(&d, fb + addr, bpp);
                const uint32_t res = Rop3(rop, 0u, color, d) & pmask;
                std::memcpy(fb + addr, &res, bpp);
            }
        }
        if (y_major) y += step_y; else x += step_x;
        err -= dmin;
        if (err < 0) {
            err += dmaj;
            if (y_major) x += step_x; else y += step_y;
        }
    }
}
