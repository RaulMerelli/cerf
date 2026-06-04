#include "mediaq_mq1188_ge.h"

#include "mediaq_mq1188.h"
#include "../../core/log.h"

#include <cstring>

/* Colour BitBLT with the source streamed through the Source FIFO (ddi.dll
   sub_1845A9C). The source is 16-bpp pixels packed two per dword, height rows
   of dwordsPerRow each; the first valid pixel of every row sits at sub-dword
   offset GE09R bit 4 (= srcLeft & 1). */
void MediaQMq1188Ge::BlitColorSource(const uint32_t* r) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = r[kGe0ADstStride] & 0x3FFu;
    const uint32_t base   = r[kGe0BBase] & 0xFFFFFu;
    if (bpp != 2u || stride == 0u) {
        LOG(Caution, "MediaQ GE: colour-source blit with unsupported depth/stride "
                     "(bpp=%u stride=%u) -- only 16 bpp implemented\n", bpp, stride);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const uint32_t size = r[kGe01Size];
    const uint32_t w = size & 0xFFFu;
    const uint32_t h = (size >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return;
    if (src_fifo_.size() < static_cast<size_t>(h)) return;

    const uint32_t dwords_per_row = static_cast<uint32_t>(src_fifo_.size()) / h;
    const uint32_t start_px = (r[kGe09SrcStride] >> 4) & 1u;  /* GE09R bit 4. */
    const uint8_t  rop = static_cast<uint8_t>(r[kGe00Command] & kCmdRopMask);

    const uint32_t xy = r[kGe02DstXY];
    const uint32_t dx = xy & 0xFFFu;
    const uint32_t dy = (xy >> 16) & 0xFFFu;

    const bool trans = (r[kGe00Command] & kCmdColorTrans) != 0u;
    const uint32_t key = r[kGe04ColorCmp] & 0xFFFFu;

    uint8_t* const fb     = owner_.FbMutableBytes();
    const uint32_t fbsize = owner_.FbSize();

    for (uint32_t row = 0; row < h; ++row) {
        const uint32_t* line = &src_fifo_[static_cast<size_t>(row) * dwords_per_row];
        if ((start_px + w + 1u) / 2u > dwords_per_row) break;  /* malformed stream. */
        for (uint32_t col = 0; col < w; ++col) {
            const uint32_t slot = start_px + col;
            const uint32_t dword = line[slot >> 1];
            const uint32_t px = (slot & 1u) ? (dword >> 16) & 0xFFFFu : dword & 0xFFFFu;
            if (trans && px == key) continue;
            const uint64_t addr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(dy + row) * stride +
                static_cast<uint64_t>(dx + col) * bpp;
            if (addr + bpp > fbsize) continue;
            uint32_t d = 0u;
            std::memcpy(&d, fb + addr, bpp);
            const uint32_t res = Rop3(rop, 0u, px, d) & 0xFFFFu;
            std::memcpy(fb + addr, &res, bpp);
        }
    }
}

/* Monochrome->colour BitBLT, 1-bpp source streamed via the Source FIFO. Source
   bit for dest (row,col) is bitOff0 + row*strideBits + col, MSB-first within
   each byte (GE0AR bit 28 = 0); bitOff0 = GE09R[4:0], strideBits = width +
   GE09R[31:25] (ddi.dll sub_184550C). Set bit -> GE07R fg, clear -> GE08R bg. */
void MediaQMq1188Ge::BlitMonoSource(const uint32_t* r) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = r[kGe0ADstStride] & 0x3FFu;
    const uint32_t base   = r[kGe0BBase] & 0xFFFFFu;
    if (bpp != 2u || stride == 0u) {
        LOG(Caution, "MediaQ GE: mono-source blit with unsupported depth/stride "
                     "(bpp=%u stride=%u) -- only 16 bpp implemented\n", bpp, stride);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const uint32_t size = r[kGe01Size];
    const uint32_t w = size & 0xFFFu;
    const uint32_t h = (size >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return;

    const uint32_t g09 = r[kGe09SrcStride];
    const uint32_t bit_off0 = g09 & 0x1Fu;
    const uint32_t stride_bits = w + (g09 >> 25);

    /* A dword count below the contiguous small-glyph bitstream's implied size is
       the larger per-row-padded source layout (ddi.dll sub_184550C general
       path), unimplemented; reading it as contiguous corrupts the frame. */
    const uint32_t total_bits = bit_off0 + (h - 1u) * stride_bits + w;
    const uint32_t need_dw = (total_bits + 31u) / 32u;
    if (src_fifo_.size() < need_dw) {
        LOG(Caution, "MediaQ GE: mono-source general-path layout not implemented "
                     "(have=%zu need=%u w=%u h=%u sb=%u off=%u)\n",
            src_fifo_.size(), need_dw, w, h, stride_bits, bit_off0);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const uint8_t  rop = static_cast<uint8_t>(r[kGe00Command] & kCmdRopMask);
    const uint32_t fg  = r[kGe07FgColor] & 0xFFFFu;
    const uint32_t bg  = r[kGe08BgColor] & 0xFFFFu;
    const bool mono_trans  = (r[kGe00Command] & kCmdMonoTrans) != 0u;
    const bool trans_clear = mono_trans && (r[kGe00Command] & kCmdMonoTrPol) == 0u;
    const bool trans_set   = mono_trans && (r[kGe00Command] & kCmdMonoTrPol) != 0u;

    const uint32_t xy = r[kGe02DstXY];
    const uint32_t dx = xy & 0xFFFu;
    const uint32_t dy = (xy >> 16) & 0xFFFu;

    uint8_t* const fb     = owner_.FbMutableBytes();
    const uint32_t fbsize = owner_.FbSize();

    for (uint32_t row = 0; row < h; ++row) {
        for (uint32_t col = 0; col < w; ++col) {
            const uint32_t bit_idx  = bit_off0 + row * stride_bits + col;
            const uint32_t byte_idx = bit_idx >> 3;
            const uint32_t bit = (src_fifo_[byte_idx >> 2] >>
                (8u * (byte_idx & 3u) + (7u - (bit_idx & 7u)))) & 1u;
            if (bit && trans_set) continue;
            if (!bit && trans_clear) continue;
            const uint32_t color = bit ? fg : bg;
            const uint64_t addr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(dy + row) * stride +
                static_cast<uint64_t>(dx + col) * bpp;
            if (addr + bpp > fbsize) continue;
            uint32_t d = 0u;
            std::memcpy(&d, fb + addr, bpp);
            const uint32_t res = Rop3(rop, 0u, color, d) & 0xFFFFu;
            std::memcpy(fb + addr, &res, bpp);
        }
    }
}

/* Screen-to-screen colour BitBLT (srcSys=0, monoSrc=0): src and dst are
   sub-rectangles of one display image (single base GE0BR, Reg 4-98) and share
   its stride GE0AR[9:0] — GE09R[9:0] is not the framebuffer pitch here.
   GE00R[11]/[12] direction makes overlapping moves read-before-overwrite. */
void MediaQMq1188Ge::BlitColorFromDisplay(uint32_t cmd) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = DestStrideBytes();
    const uint32_t base   = BaseAddr();
    if (bpp != 2u || stride == 0u) {
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
    const int xstep = (cmd & kCmdXNeg) ? -1 : 1;   /* GE00R[11]. */
    const int ystep = (cmd & kCmdYNeg) ? -1 : 1;   /* GE00R[12]. */
    const uint8_t rop = static_cast<uint8_t>(cmd & kCmdRopMask);
    const bool trans  = (cmd & kCmdColorTrans) != 0u;
    const uint32_t key = reg_[kGe04ColorCmp] & 0xFFFFu;
    const bool clip = (cmd & kCmdEnClip) != 0u;
    const int cl = static_cast<int>(reg_[kGe05ClipLT] & 0xFFFu);
    const int ct = static_cast<int>((reg_[kGe05ClipLT] >> 16) & 0xFFFu);
    const int cr = static_cast<int>(reg_[kGe06ClipRB] & 0xFFFu);
    const int cb = static_cast<int>((reg_[kGe06ClipRB] >> 16) & 0xFFFu);

    uint8_t* const fb     = owner_.FbMutableBytes();
    const uint32_t fbsize = owner_.FbSize();

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
            if (trans && (s & 0xFFFFu) == key) continue;
            uint32_t d = 0u;
            std::memcpy(&d, fb + daddr, bpp);
            const uint32_t res = Rop3(rop, 0u, s, d) & 0xFFFFu;
            std::memcpy(fb + daddr, &res, bpp);
        }
    }
}

/* Mono-source BitBLT with the 1-bpp source in display memory (srcSys=0,
   monoSrc=1): the source bit for each pixel is at GE0BR + srcY*GE09R[9:0]
   stride, bit (GE09R[27:25]+srcX) MSB-first (Reg 4-95); set -> GE07R fg, clear
   -> GE08R bg, under the ROP and optional mono transparency. */
void MediaQMq1188Ge::BlitMonoFromDisplay(uint32_t cmd) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = DestStrideBytes();
    const uint32_t base   = BaseAddr();
    if (bpp != 2u || stride == 0u) {
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
    const uint32_t src_stride = reg_[kGe09SrcStride] & 0x3FFu;        /* GE09R[9:0] bytes. */
    const uint32_t bit_off0   = (reg_[kGe09SrcStride] >> 25) & 0x7u;  /* GE09R[27:25]. */
    const uint8_t rop = static_cast<uint8_t>(cmd & kCmdRopMask);
    const uint32_t fg = reg_[kGe07FgColor] & 0xFFFFu;
    const uint32_t bg = reg_[kGe08BgColor] & 0xFFFFu;
    const bool mono_trans  = (cmd & kCmdMonoTrans) != 0u;
    const bool trans_clear = mono_trans && (cmd & kCmdMonoTrPol) == 0u;
    const bool trans_set   = mono_trans && (cmd & kCmdMonoTrPol) != 0u;
    const bool clip = (cmd & kCmdEnClip) != 0u;
    const int cl = static_cast<int>(reg_[kGe05ClipLT] & 0xFFFu);
    const int ct = static_cast<int>((reg_[kGe05ClipLT] >> 16) & 0xFFFu);
    const int cr = static_cast<int>(reg_[kGe06ClipRB] & 0xFFFu);
    const int cb = static_cast<int>((reg_[kGe06ClipRB] >> 16) & 0xFFFu);

    uint8_t* const fb     = owner_.FbMutableBytes();
    const uint32_t fbsize = owner_.FbSize();

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
            const uint32_t res = Rop3(rop, 0u, color, d) & 0xFFFFu;
            std::memcpy(fb + daddr, &res, bpp);
        }
    }
}

void MediaQMq1188Ge::FillSolid(uint8_t rop, uint32_t color, uint32_t cmd) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = DestStrideBytes();
    const uint32_t base   = BaseAddr();
    if (bpp == 0u || stride == 0u) {
        LOG(Caution, "MediaQ GE: solid fill with unsupported depth/stride "
                     "(GE0AR=0x%08X)\n", reg_[kGe0ADstStride]);
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

    uint8_t* const fb     = owner_.FbMutableBytes();
    const uint32_t fbsize = owner_.FbSize();
    const uint32_t pmask  = (bpp >= 4u) ? 0xFFFFFFFFu : ((1u << (bpp * 8u)) - 1u);

    for (uint32_t row = 0; row < h; ++row) {
        const uint32_t yy = y + row;
        if (clip && (yy < ct || yy > cb)) continue;
        for (uint32_t col = 0; col < w; ++col) {
            const uint32_t xx = x + col;
            if (clip && (xx < cl || xx > cr)) continue;
            const uint64_t addr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(yy) * stride +
                static_cast<uint64_t>(xx) * bpp;
            if (addr + bpp > fbsize) continue;
            uint32_t d = 0u;
            std::memcpy(&d, fb + addr, bpp);
            const uint32_t res = Rop3(rop, color, color, d) & pmask;
            std::memcpy(fb + addr, &res, bpp);
        }
    }
}

/* Bresenham hardware line draw (GE00R type[10:8]=100; datasheet §4.11,
   Reg 4-83..4-89). Per-field bit decode cited inline below. */
void MediaQMq1188Ge::DrawLine(uint32_t cmd) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = DestStrideBytes();
    const uint32_t base   = BaseAddr();
    if (bpp == 0u || stride == 0u) {
        LOG(Caution, "MediaQ GE: line draw with unsupported depth/stride "
                     "(GE0AR=0x%08X)\n", reg_[kGe0ADstStride]);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    /* Line colour is a solid source (GE07R) or solid pattern (GE12R); GDI pen
       drawing never textures a line from the Source FIFO / pattern bitmap. */
    uint32_t color;
    if (cmd & kCmdSolidSrc)      color = reg_[kGe07FgColor] & 0xFFFFu;
    else if (cmd & kCmdSolidPat) color = reg_[kGe12PatFg] & 0xFFFFu;
    else {
        LOG(Caution, "MediaQ GE: non-solid line source not implemented "
                     "(cmd=0x%08X)\n", cmd);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

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
        y_major = (q & 1u) != 0u;                                   /* bit0: y-major. */
        step_x  = (q & 2u) ? -1 : 1;                                /* bit1: x negative. */
        step_y  = (q & 4u) ? -1 : 1;                                /* bit2: y negative. */
    } else {                                                        /* GE01R[31]=1: direction-bit mode. */
        y_major = (reg_[kGe01Size] & (1u << 29)) != 0u;            /* GE01R[29]. */
        step_x  = (cmd & kCmdXNeg) ? -1 : 1;                        /* GE00R[11]. */
        step_y  = (cmd & kCmdYNeg) ? -1 : 1;                        /* GE00R[12]. */
    }

    const bool clip = (cmd & kCmdEnClip) != 0u;
    const int  cl = static_cast<int>(reg_[kGe05ClipLT] & 0xFFFu);
    const int  ct = static_cast<int>((reg_[kGe05ClipLT] >> 16) & 0xFFFu);
    const int  cr = static_cast<int>(reg_[kGe06ClipRB] & 0xFFFu);
    const int  cb = static_cast<int>((reg_[kGe06ClipRB] >> 16) & 0xFFFu);

    uint8_t* const fb     = owner_.FbMutableBytes();
    const uint32_t fbsize = owner_.FbSize();
    const uint32_t pmask  = (bpp >= 4u) ? 0xFFFFFFFFu : ((1u << (bpp * 8u)) - 1u);

    /* Major length = number of major steps; +1 pixel unless the last is dropped. */
    const int count = dmaj + (no_last ? 0 : 1);
    int err = dmaj >> 1;
    for (int i = 0; i < count; ++i) {
        if (x >= 0 && y >= 0 &&
            !(clip && (x < cl || x > cr || y < ct || y > cb))) {
            const uint64_t addr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(y) * stride +
                static_cast<uint64_t>(x) * bpp;
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
