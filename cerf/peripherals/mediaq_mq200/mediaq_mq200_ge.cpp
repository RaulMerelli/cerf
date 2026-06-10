#include "mediaq_mq200_ge.h"

#include "../../core/log.h"

#include <cstring>

const MediaQGe::Layout& MediaQMq200Ge::Lyt() const {
    /* fg=GE42R, bg=GE43R, pat_fg=GE42R; src_stride=GE09R; GE0AR stride[11:0]
       (Table 5-73), GE0BR base[20:0] (Table 5-74). */
    static const Layout kL{0x42u, 0x43u, 0x42u, 0x09u, 0xFFFu, 0x1FFFFFu};
    return kL;
}

/* ddi.dll's rectangle fill (sub_13458A0 / sub_1345944) issues a BitBLT with
   mono pattern (GE00R[15]) + solid (GE00R[23]); the pattern colour is GE42R. */
bool MediaQMq200Ge::IsSolidFill(uint32_t cmd) const {
    return (cmd & kCmdMonoPat) != 0u && (cmd & kCmdSolidSrc) != 0u;
}

uint32_t MediaQMq200Ge::SolidFillColor() const {
    return reg_[kGe42PatFg];
}

uint32_t MediaQMq200Ge::LineColor(uint32_t cmd) const {
    /* Mono pattern/source line -> GE42R; flat colour line (solid, no mono) ->
       GE08R colour foreground (Data Book GE08R). */
    if ((cmd & kCmdMonoPat) || (cmd & kCmdMonoSrc)) return reg_[kGe42PatFg];
    if (cmd & kCmdSolidSrc) return reg_[kGe08FgColor];
    LOG(Caution, "MediaQ MQ200 GE: non-solid line source (cmd=0x%08X)\n", cmd);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

/* Colour source: 16-bpp pixels byte-packed two per dword, first pixel of each
   row at dest-dword alignment dst_x & 1 (ddi.dll sub_1346EF8). MQ-200 system
   source is colour-only, so mono returns 0 (handled in BlitMonoSource). */
uint32_t MediaQMq200Ge::ExpectedSourceDwords() const {
    const uint32_t w = reg_[kGe01Size] & 0xFFFu;
    const uint32_t h = (reg_[kGe01Size] >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return 0u;
    if (reg_[kGe00Command] & kCmdMonoSrc) {
        /* Packed 1-bpp bitstream, byte-aligned rows (GE09R packed: [5:3] byte,
           [2:0] bit initial offset, Data Book Table 5-72); the driver may pad
           the tail, but the blit needs only enough dwords to cover the bits. */
        const uint32_t g09 = reg_[kGe09SrcOff];
        const uint32_t bit_off0 = ((g09 >> 3) & 7u) * 8u + (g09 & 7u);
        const uint32_t stride_bits = ((w + 7u) / 8u) * 8u;
        const uint32_t total_bits = bit_off0 + (h - 1u) * stride_bits + w;
        return (total_bits + 31u) / 32u;
    }
    const uint32_t start_px = reg_[kGe02DstXY] & 1u;
    const uint32_t dwords_per_row = (start_px + ((start_px + w) & 1u) + w) / 2u;
    return dwords_per_row * h;
}

void MediaQMq200Ge::BlitColorSource(const uint32_t* r) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = r[kGe0ADstStride] & Lyt().stride_mask;
    const uint32_t base   = r[kGe0BBase] & Lyt().base_mask;
    if (bpp != 2u || stride == 0u) {
        LOG(Caution, "MediaQ MQ200 GE: colour-source blit unsupported depth/stride "
                     "(bpp=%u stride=%u)\n", bpp, stride);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const uint32_t size = r[kGe01Size];
    const uint32_t w = size & 0xFFFu;
    const uint32_t h = (size >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return;
    if (src_fifo_.size() < static_cast<size_t>(h)) return;

    const uint32_t dwords_per_row = static_cast<uint32_t>(src_fifo_.size()) / h;
    const uint32_t start_px = r[kGe02DstXY] & 1u;       /* dest-dword alignment. */
    const uint8_t  rop = static_cast<uint8_t>(r[kGe00Command] & kCmdRopMask);

    const uint32_t xy = r[kGe02DstXY];
    const uint32_t dx = xy & 0xFFFu;
    const uint32_t dy = (xy >> 16) & 0xFFFu;

    const bool trans = (r[kGe00Command] & kCmdColorTrans) != 0u;
    const uint32_t key = r[kGe04ColorCmp] & 0xFFFFu;

    uint8_t* const fb     = Fb();
    const uint32_t fbsize = FbBytes();

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

/* Mono->colour BitBLT, packed 1-bpp source via Source FIFO (GE00R[20]=1). Bit for
   dest (row,col) = bit_off0 + row*stride_bits + col, MSB-first per byte; bit_off0
   = GE09R[5:3]*8 + [2:0], stride_bits = ceil(w/8)*8 (byte-aligned rows). Set bit
   -> GE42R fg, clear -> GE43R bg. */
void MediaQMq200Ge::BlitMonoSource(const uint32_t* r) {
    const uint32_t bpp    = BytesPerPixel();
    const uint32_t stride = r[kGe0ADstStride] & Lyt().stride_mask;
    const uint32_t base   = r[kGe0BBase] & Lyt().base_mask;
    if (bpp != 2u || stride == 0u) {
        LOG(Caution, "MediaQ MQ200 GE: mono-source blit unsupported depth/stride "
                     "(bpp=%u stride=%u)\n", bpp, stride);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const uint32_t size = r[kGe01Size];
    const uint32_t w = size & 0xFFFu;
    const uint32_t h = (size >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return;

    const uint32_t g09 = r[kGe09SrcOff];
    const uint32_t bit_off0 = ((g09 >> 3) & 7u) * 8u + (g09 & 7u);
    const uint32_t stride_bits = ((w + 7u) / 8u) * 8u;
    const uint32_t total_bits = bit_off0 + (h - 1u) * stride_bits + w;
    if (src_fifo_.size() < (total_bits + 31u) / 32u) return;

    const uint8_t  rop = static_cast<uint8_t>(r[kGe00Command] & kCmdRopMask);
    const uint32_t fg  = r[kGe42PatFg] & 0xFFFFu;
    const uint32_t bg  = r[kGe43PatBg] & 0xFFFFu;
    const bool mono_trans  = (r[kGe00Command] & kCmdMonoTrans) != 0u;
    const bool trans_clear = mono_trans && (r[kGe00Command] & kCmdMonoTrPol) == 0u;
    const bool trans_set   = mono_trans && (r[kGe00Command] & kCmdMonoTrPol) != 0u;

    const uint32_t xy = r[kGe02DstXY];
    const uint32_t dx = xy & 0xFFFu;
    const uint32_t dy = (xy >> 16) & 0xFFFu;

    const bool clip = (r[kGe00Command] & kCmdEnClip) != 0u;
    const uint32_t cl = r[kGe05ClipLT] & 0xFFFu;
    const uint32_t ct = (r[kGe05ClipLT] >> 16) & 0xFFFu;
    const uint32_t cr = r[kGe06ClipRB] & 0xFFFu;
    const uint32_t cb = (r[kGe06ClipRB] >> 16) & 0xFFFu;

    uint8_t* const fb     = Fb();
    const uint32_t fbsize = FbBytes();

    for (uint32_t row = 0; row < h; ++row) {
        const uint32_t yy = dy + row;
        if (clip && (yy < ct || yy > cb)) continue;
        for (uint32_t col = 0; col < w; ++col) {
            const uint32_t xx = dx + col;
            if (clip && (xx < cl || xx > cr)) continue;
            const uint32_t bit_idx  = bit_off0 + row * stride_bits + col;
            const uint32_t byte_idx = bit_idx >> 3;
            const uint32_t bit = (src_fifo_[byte_idx >> 2] >>
                (8u * (byte_idx & 3u) + (7u - (bit_idx & 7u)))) & 1u;
            if (bit && trans_set) continue;
            if (!bit && trans_clear) continue;
            const uint32_t color = bit ? fg : bg;
            const uint64_t addr = static_cast<uint64_t>(base) +
                static_cast<uint64_t>(yy) * stride + static_cast<uint64_t>(xx) * bpp;
            if (addr + bpp > fbsize) continue;
            uint32_t d = 0u;
            std::memcpy(&d, fb + addr, bpp);
            const uint32_t res = Rop3(rop, 0u, color, d) & 0xFFFFu;
            std::memcpy(fb + addr, &res, bpp);
        }
    }
}
