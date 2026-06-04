#include "mediaq_mq1188_ge.h"

#include "mediaq_mq1188.h"
#include "../../core/log.h"

#include <cstring>

void MediaQMq1188Ge::WriteReg(uint32_t g, uint32_t value) {
    if (g >= kNumRegs) return;
    reg_[g] = value;
    if (g == kGe00Command) Execute();
}

uint32_t MediaQMq1188Ge::BytesPerPixel() const {
    /* GE0AR[31:30] colour depth: 00 = 8 bpp, 01 = 16 bpp (Reg 4-97). */
    switch ((reg_[kGe0ADstStride] >> 30) & 3u) {
        case 0:  return 1u;
        case 1:  return 2u;
        default: return 0u;
    }
}

/* 3-operand raster op (Reg 4-83 [7:0]): result bit = rop[(P<<2)|(S<<1)|D],
   evaluated bitwise across the pixel word. */
uint32_t MediaQMq1188Ge::Rop3(uint8_t rop, uint32_t p, uint32_t s, uint32_t d) {
    uint32_t r = 0u;
    if (rop & 0x01u) r |= ~p & ~s & ~d;
    if (rop & 0x02u) r |= ~p & ~s &  d;
    if (rop & 0x04u) r |= ~p &  s & ~d;
    if (rop & 0x08u) r |= ~p &  s &  d;
    if (rop & 0x10u) r |=  p & ~s & ~d;
    if (rop & 0x20u) r |=  p & ~s &  d;
    if (rop & 0x40u) r |=  p &  s & ~d;
    if (rop & 0x80u) r |=  p &  s &  d;
    return r;
}

void MediaQMq1188Ge::Execute() {
    /* A new command means the prior system-source command's source stream has
       completed; draw it now (using its register snapshot) before the live
       registers are overwritten. */
    if (pending_active_) ExecutePending();

    const uint32_t cmd  = reg_[kGe00Command];
    const uint32_t type = (cmd >> kCmdTypeShift) & kCmdTypeMask;
    const uint8_t  rop  = static_cast<uint8_t>(cmd & kCmdRopMask);

    const bool solid_pat = (cmd & kCmdSolidPat) != 0u;
    const bool src_sys   = (cmd & kCmdSrcSystem) != 0u;
    const bool mono_src  = (cmd & kCmdMonoSrc) != 0u;

    if (type == kTypeBitBlt && src_sys) {
        /* Source streams in after this command; latch a snapshot and draw it
           from PushSourceFifo once the expected source-dword count arrives. */
        std::memcpy(pending_reg_, reg_, sizeof(reg_));
        src_fifo_.clear();
        expected_dwords_ = ExpectedSourceDwords();
        pending_active_ = true;
        if (expected_dwords_ == 0u) ExecutePending();   /* empty blit. */
        return;
    }

    if (type == kTypeBitBlt && solid_pat) {
        FillSolid(rop, reg_[kGe12PatFg], cmd);
        return;
    }

    if (type == kTypeLine) {
        DrawLine(cmd);
        return;
    }

    /* Every command class the Falcon ROM exercises is handled above;
       screen-to-screen copy is not yet implemented. Dropping such a blit
       would be a silent fake-success that corrupts the frame and costs hours
       to trace, so halt loudly naming the command. */
    LOG(Caution, "MediaQ GE: unimplemented command 0x%08X (type=%u rop=0x%02X "
                 "solidPat=%u srcSys=%u monoSrc=%u)\n",
        cmd, type, rop, solid_pat ? 1u : 0u, src_sys ? 1u : 0u,
        mono_src ? 1u : 0u);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void MediaQMq1188Ge::PushSourceFifo(uint32_t value) {
    src_fifo_.push_back(value);
    if (pending_active_ && src_fifo_.size() >= expected_dwords_) ExecutePending();
}

void MediaQMq1188Ge::FlushPending() {
    if (pending_active_ && src_fifo_.size() >= expected_dwords_) ExecutePending();
}

/* Source dwords the latched system-source blit will stream: contiguous 1-bpp
   bits for a mono source (sub_184550C), or 16-bpp pixels two-per-dword for a
   colour source (sub_1845A9C), with the first pixel at GE09R bit 4. */
uint32_t MediaQMq1188Ge::ExpectedSourceDwords() const {
    const uint32_t w = reg_[kGe01Size] & 0xFFFu;
    const uint32_t h = (reg_[kGe01Size] >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return 0u;
    if (reg_[kGe00Command] & kCmdMonoSrc) {
        const uint32_t g09 = reg_[kGe09SrcStride];
        const uint32_t bit_off0 = g09 & 0x1Fu;
        const uint32_t stride_bits = w + (g09 >> 25);
        const uint32_t total_bits = bit_off0 + (h - 1u) * stride_bits + w;
        return (total_bits + 31u) / 32u;
    }
    const uint32_t start_px = (reg_[kGe09SrcStride] >> 4) & 1u;
    const uint32_t dwords_per_row = (start_px + ((start_px + w) & 1u) + w) / 2u;
    return dwords_per_row * h;
}

void MediaQMq1188Ge::ExecutePending() {
    pending_active_ = false;
    if (pending_reg_[kGe00Command] & kCmdMonoSrc)
        BlitMonoSource(pending_reg_);
    else
        BlitColorSource(pending_reg_);
    src_fifo_.clear();
}

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

    /* The implemented encoding is the contiguous small-glyph bitstream; if the
       streamed dwords do not match its implied size the source is the larger
       per-row-padded layout (ddi.dll sub_184550C general path), which is not
       implemented -- halt rather than misread it into frame-buffer corruption. */
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

    /* Line colour: solid source (GE07R) or solid pattern (GE12R). A line
       textured from the Source FIFO / pattern bitmap is never emitted by GDI
       pen drawing; halt honestly rather than mis-draw it. */
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
