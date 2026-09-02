#define NOMINMAX

#include "siemens_mp377_sm501_blitter.h"
#include "siemens_mp377_sm501_fb.h"
#include "siemens_mp377_sm501_internal.h"

#include "../../boards/board_context.h"

#include "../../core/cerf_emulator.h"

#include <algorithm>
#include <cstdint>
#include <vector>
namespace siemens_mp377 {

bool SiemensMp377Sm501Blitter::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377Sm501Blitter::ExecuteCommand(uint32_t command) {
    ExecuteDdiVgx2dCommand(command);
}

void SiemensMp377Sm501Blitter::WriteDataPort(uint32_t value) {
    HandleDdiVgxDataPortWord(value);
}

uint32_t SiemensMp377Sm501Blitter::R(uint32_t off) const {
    return emu_.Get<SiemensMp377Sm501Regs>().ReadSm501Register(off);
}
uint32_t SiemensMp377Sm501Blitter::SrcPitchField(uint32_t v) {
    return v & k2dPitchMask;
}
uint32_t SiemensMp377Sm501Blitter::DstPitchField(uint32_t v) {
    return (v >> 16) & k2dPitchMask;
}
uint32_t SiemensMp377Sm501Blitter::Lo16(uint32_t v) {
    return v & 0xFFFFu;
}
uint32_t SiemensMp377Sm501Blitter::Hi16(uint32_t v) {
    return (v >> 16) & 0xFFFFu;
}
void SiemensMp377Sm501Blitter::Log2dCommand(uint32_t, const SiemensMp377Sm501Blitter::State2d&, const char*) {
    /* Quiet in normal MP377 builds. */
}
uint32_t SiemensMp377Sm501Blitter::DecodePitchField(uint32_t p, uint32_t fallback) const {
    return (p >= 1u && p <= 8192u) ? p : fallback;
}
uint32_t SiemensMp377Sm501Blitter::DecodeDstPitchPixels() const {
    return DecodePitchField(DstPitchField(R(k2dPitch)), kFbWidth);
}
uint32_t SiemensMp377Sm501Blitter::DecodeSrcPitchPixels() const {
    return DecodePitchField(SrcPitchField(R(k2dPitch)), DecodeDstPitchPixels());
}
SiemensMp377Sm501Blitter::SurfaceState SiemensMp377Sm501Blitter::DecodeSurface(bool source, uint32_t cmd) const {
    SiemensMp377Sm501Blitter::SurfaceState s;
    const uint32_t raw_base = R(source ? k2dSourceBase : k2dDestinationBase);
    s.base = NormalizeFbOffset(raw_base);
    s.pitch_pixels = source ? DecodeSrcPitchPixels() : DecodeDstPitchPixels();
    const bool mono = source && cmd == kDdiHostMonoTransparentCommand;
    const uint32_t bpp = mono ? 1u : 16u;
    s.pitch_bytes = std::max<uint32_t>(1u, (s.pitch_pixels * bpp + 7u) / 8u);
    if (!mono && s.pitch_bytes < s.pitch_pixels * 2u) s.pitch_bytes = s.pitch_pixels * 2u;
    return s;
}
bool SiemensMp377Sm501Blitter::Rop3Bit(uint8_t rop, bool p, bool s, bool d) {
    const unsigned idx = (p ? 4u : 0u) | (s ? 2u : 0u) | (d ? 1u : 0u);
    return ((rop >> idx) & 1u) != 0;
}
bool SiemensMp377Sm501Blitter::RopDependsOnSource(uint8_t rop) {
    for (unsigned p = 0; p < 2; ++p)
        for (unsigned d = 0; d < 2; ++d)
            if (Rop3Bit(rop, p != 0, false, d != 0) != Rop3Bit(rop, p != 0, true, d != 0)) return true;
    return false;
}
SiemensMp377Sm501Blitter::State2d SiemensMp377Sm501Blitter::DecodeState2d(uint32_t cmd) const {
    const uint32_t src_xy = R(k2dSource);
    const uint32_t dst_xy = R(k2dDestination);
    const uint32_t extent = R(k2dDimension);
    SiemensMp377Sm501Blitter::State2d st;
    st.src_x = Hi16(src_xy);
    st.src_y = Lo16(src_xy);
    st.dst_x = Hi16(dst_xy);
    st.dst_y = Lo16(dst_xy);
    st.width = Hi16(extent);
    st.height = Lo16(extent);
    st.src_surface = DecodeSurface(true, cmd);
    st.dst_surface = DecodeSurface(false, cmd);
    st.src_pitch = st.src_surface.pitch_pixels;
    st.dst_pitch = st.dst_surface.pitch_pixels;
    st.mono_fg_state = DecodeDdiMonoColor();
    st.fill_color = DecodeDdiFillColor().rgb565;
    st.inv_fg = DecodeDdiColorRegister(k2dBackground).rgb565;
    st.rop = static_cast<uint8_t>(cmd & 0xFFu);
    st.backwards = (cmd & 0x08000000u) != 0;
    return st;
}
uint32_t SiemensMp377Sm501Blitter::NormalizeFbOffset(uint32_t v) const {
    uint32_t off = 0;
    if (Sm501FbPaToOffset(v, off)) return off;
    if (v < kSm501FbBytes) return v;
    return 0u;
}
void SiemensMp377Sm501Blitter::FillRect16(const SiemensMp377Sm501Blitter::State2d& st, uint16_t color) {
    auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
    if (!fb) return;
    uint8_t* vram = fb->MutableVramFor2d();
    if (!vram) return;
    const SiemensMp377Sm501Blitter::SurfaceState& dst = st.dst_surface;
    const uint32_t surface_w = SurfaceWidthPixels16(dst);
    const uint32_t surface_h = SurfaceHeightRows(dst);
    if (surface_w == 0 || surface_h == 0) return;
    if (st.dst_x >= surface_w || st.dst_y >= surface_h) return;
    const uint32_t width = std::min(st.width, surface_w - st.dst_x);
    const uint32_t height = std::min(st.height, surface_h - st.dst_y);
    if (width == 0 || height == 0) return;
    const uint32_t stride = dst.pitch_bytes ? dst.pitch_bytes : st.dst_pitch * 2u;
    if (stride == 0) return;
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t row = dst.base + (st.dst_y + y) * stride + st.dst_x * 2u;
        if (row >= kSm501FbBytes) break;
        const uint32_t row_bytes = std::min(width * 2u, kSm501FbBytes - row);
        for (uint32_t x = 0; x + 1u < row_bytes; x += 2u) {
            const uint16_t d = static_cast<uint16_t>(vram[row + x] | (vram[row + x + 1u] << 8));
            const uint16_t out = ApplyDdiRop16(st.rop, color, d, color);
            vram[row + x] = static_cast<uint8_t>(out);
            vram[row + x + 1u] = static_cast<uint8_t>(out >> 8);
        }
        fb->Note2dWrite(row, row_bytes);
    }
}
SiemensMp377Sm501Blitter::ColorState SiemensMp377Sm501Blitter::DecodeDdiColorRegister(uint32_t off) const {
    return {static_cast<uint16_t>(R(off) & 0xFFFFu), true};
}
SiemensMp377Sm501Blitter::ColorState SiemensMp377Sm501Blitter::DecodeDdiFillColor() const {
    return DecodeDdiColorRegister(k2dForeground);
}
SiemensMp377Sm501Blitter::ColorState SiemensMp377Sm501Blitter::DecodeDdiMonoColor() const {
    return DecodeDdiColorRegister(k2dForeground);
}
bool SiemensMp377Sm501Blitter::IsDdiVgxHostDataCommand(uint32_t cmd) const {
    if (cmd == kDdiHostMonoTransparentCommand) return true;
    if ((cmd & kDdiHostColorCommandMask) == kDdiHostColorCommandBase) return true;
    return false;
}
void SiemensMp377Sm501Blitter::BeginDdiVgxHostDataCommand(uint32_t cmd) {
    host_data_active_ = true;
    host_data_mono_ = (cmd == kDdiHostMonoTransparentCommand);
    const uint32_t dst_xy = R(k2dDestination);
    const uint32_t wh = R(k2dDimension);
    host_dst_x_ = Hi16(dst_xy);
    host_dst_y_ = Lo16(dst_xy);
    host_width_ = Hi16(wh);
    host_height_ = Lo16(wh);
    if (host_width_ == 0) host_width_ = 1;
    if (host_height_ == 0) host_height_ = 1;
    if (host_width_ > 2048u) host_width_ = 2048u;
    if (host_height_ > 2048u) host_height_ = 2048u;
    const SiemensMp377Sm501Blitter::State2d st = DecodeState2d(cmd);
    host_dst_pitch_bytes_ = st.dst_surface.pitch_bytes;
    host_dst_surface_width_ = SurfaceWidthPixels16(st.dst_surface);
    host_dst_surface_height_ = SurfaceHeightRows(st.dst_surface);
    host_dst_base_ = st.dst_surface.base;
    host_fg_ = st.mono_fg_state.rgb565;
    host_bg_ = st.inv_fg;
    host_mono_transparent_ = (cmd == kDdiHostMonoTransparentCommand);
    host_y_ = 0;
    host_src_byte_in_row_ = 0;
    host_src_bit_offset_ = 0;
    host_row_bytes_.clear();
    if (host_data_mono_) {
        const uint32_t src_field = R(k2dSource);
        host_src_bit_offset_ = (src_field >> 16) & 7u;
        const uint32_t active_bits = host_src_bit_offset_ + host_width_;
        host_src_active_bytes_ = (active_bits + 7u) / 8u;
        host_src_pitch_bytes_ = (host_src_active_bytes_ + 3u) & ~3u;
    } else {
        host_src_bit_offset_ = 0;
        host_src_active_bytes_ = (host_width_ * kDdiHostColorBpp + 7u) / 8u;
        host_src_pitch_bytes_ = (host_src_active_bytes_ + 7u) & ~7u;
        host_row_bytes_.clear();
        host_row_bytes_.reserve(host_src_pitch_bytes_);
    }
}
void SiemensMp377Sm501Blitter::CompleteHostDataIfDone() {
    if (!host_data_active_) return;
    if (host_y_ >= host_height_) {
        host_data_active_ = false;
        host_row_bytes_.clear();
    }
}
void SiemensMp377Sm501Blitter::HostDataWritePixel(uint32_t x, uint32_t y, uint16_t p) {
    auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
    if (!fb) return;
    const uint32_t abs_x = host_dst_x_ + x;
    const uint32_t abs_y = host_dst_y_ + y;
    if (host_dst_surface_width_ && abs_x >= host_dst_surface_width_) return;
    if (host_dst_surface_height_ && abs_y >= host_dst_surface_height_) return;
    uint8_t* vram = fb->MutableVramFor2d();
    if (!vram) return;
    const uint32_t stride = host_dst_pitch_bytes_;
    const uint32_t off = host_dst_base_ + abs_y * stride + abs_x * 2u;
    if (off + 1u >= kSm501FbBytes) return;
    vram[off] = static_cast<uint8_t>(p);
    vram[off + 1u] = static_cast<uint8_t>(p >> 8);
    fb->Note2dWrite(off, 2u);
}
void SiemensMp377Sm501Blitter::HostDataAdvanceRow() {
    host_src_byte_in_row_ = 0;
    ++host_y_;
    host_row_bytes_.clear();
    CompleteHostDataIfDone();
}
void SiemensMp377Sm501Blitter::HostDataMonoByte(uint8_t b) {
    if (!host_data_active_) return;
    if (host_src_byte_in_row_ < host_src_active_bytes_ && host_y_ < host_height_) {
        for (int bit = 7; bit >= 0; --bit) {
            const uint32_t bit_in_row = host_src_byte_in_row_ * 8u + static_cast<uint32_t>(7 - bit);
            if (bit_in_row < host_src_bit_offset_) continue;
            const uint32_t px = bit_in_row - host_src_bit_offset_;
            if (px >= host_width_) continue;
            if (b & (1u << bit)) {
                HostDataWritePixel(px, host_y_, host_fg_);
            } else if (!host_mono_transparent_) {
                HostDataWritePixel(px, host_y_, host_bg_);
            }
        }
    }
    ++host_src_byte_in_row_;
    if (host_src_byte_in_row_ >= host_src_pitch_bytes_) {
        HostDataAdvanceRow();
    }
}
uint8_t SiemensMp377Sm501Blitter::HostRowByte(uint32_t i) const {
    return i < host_row_bytes_.size() ? host_row_bytes_[i] : 0;
}
void SiemensMp377Sm501Blitter::FlushHostDataColorRow() {
    if (host_y_ >= host_height_) return;
    for (uint32_t x = 0; x < host_width_; ++x) {
        const uint32_t i = x * 2u;
        const uint16_t p = static_cast<uint16_t>(HostRowByte(i) | (HostRowByte(i + 1u) << 8));
        HostDataWritePixel(x, host_y_, p);
    }
}
void SiemensMp377Sm501Blitter::HostDataColorByte(uint8_t b) {
    if (!host_data_active_) return;
    if (host_src_byte_in_row_ < host_src_active_bytes_ && host_y_ < host_height_) host_row_bytes_.push_back(b);
    ++host_src_byte_in_row_;
    if (host_src_byte_in_row_ >= host_src_pitch_bytes_) {
        FlushHostDataColorRow();
        HostDataAdvanceRow();
    }
}
void SiemensMp377Sm501Blitter::BeginDdiPatternUpload() {
    pattern_upload_active_ = true;
    pattern_valid_ = false;
    pattern_words_.clear();
}
void SiemensMp377Sm501Blitter::FinishDdiPatternUpload() {
    if (!pattern_upload_active_) return;
    pattern_upload_active_ = false;
    pattern_valid_ = !pattern_words_.empty();
}
void SiemensMp377Sm501Blitter::HandleDdiPatternDataPortWord(uint32_t v) {
    if (!pattern_upload_active_) return;
    if (pattern_words_.size() < 128u) pattern_words_.push_back(v);
}
uint16_t SiemensMp377Sm501Blitter::PatternPixel565(uint32_t x, uint32_t y, uint16_t fallback) const {
    if (!pattern_valid_ || pattern_words_.empty()) return fallback;
    const uint32_t px = (x & 7u);
    const uint32_t py = (y & 7u);
    const uint32_t i = py * 8u + px;
    if (pattern_words_.size() >= 32u && pattern_words_.size() < 64u) {
        const uint32_t w = pattern_words_[i / 2u];
        return static_cast<uint16_t>((i & 1u) ? (w >> 16) : (w & 0xFFFFu));
    }
    if (pattern_words_.size() >= 64u) {
        const uint32_t c = pattern_words_[i];
        const uint8_t r = static_cast<uint8_t>((c >> 16) & 0xFFu);
        const uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFFu);
        const uint8_t b = static_cast<uint8_t>(c & 0xFFu);
        return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
    }
    if (pattern_words_.size() >= 16u) {
        const uint32_t w = pattern_words_[i / 4u];
        const uint8_t idx = static_cast<uint8_t>((w >> ((i & 3u) * 8u)) & 0xFFu);
        return static_cast<uint16_t>(((idx & 0xF8u) << 8) | ((idx & 0xFCu) << 3) | (idx >> 3));
    }
    return fallback;
}
void SiemensMp377Sm501Blitter::PatternFillRect16(const SiemensMp377Sm501Blitter::State2d& st, uint16_t fallback) {
    auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
    if (!fb) return;
    uint8_t* vram = fb->MutableVramFor2d();
    if (!vram) return;
    const SiemensMp377Sm501Blitter::SurfaceState& dst = st.dst_surface;
    const uint32_t surface_w = SurfaceWidthPixels16(dst);
    const uint32_t surface_h = SurfaceHeightRows(dst);
    if (surface_w == 0 || surface_h == 0) return;
    if (st.dst_x >= surface_w || st.dst_y >= surface_h) return;
    const uint32_t width = std::min(st.width, surface_w - st.dst_x);
    const uint32_t height = std::min(st.height, surface_h - st.dst_y);
    if (width == 0 || height == 0) return;
    const uint32_t stride = dst.pitch_bytes ? dst.pitch_bytes : st.dst_pitch * 2u;
    if (stride == 0) return;
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t row = dst.base + (st.dst_y + y) * stride + st.dst_x * 2u;
        if (row >= kSm501FbBytes) break;
        const uint32_t row_bytes = std::min(width * 2u, kSm501FbBytes - row);
        for (uint32_t x = 0; x + 1u < row_bytes; x += 2u) {
            const uint32_t px = x >> 1;
            const uint16_t color = PatternPixel565(px, y, fallback);
            const uint16_t d = static_cast<uint16_t>(vram[row + x] | (vram[row + x + 1u] << 8));
            const uint16_t out = ApplyDdiRop16(st.rop, color, d, color);
            vram[row + x] = static_cast<uint8_t>(out);
            vram[row + x + 1u] = static_cast<uint8_t>(out >> 8);
        }
        fb->Note2dWrite(row, row_bytes);
    }
}
void SiemensMp377Sm501Blitter::HandleDdiVgxDataPortWord(uint32_t v) {
    if (pattern_upload_active_) {
        HandleDdiPatternDataPortWord(v);
        return;
    }
    if (!host_data_active_) return;
    const uint8_t b0 = static_cast<uint8_t>(v);
    const uint8_t b1 = static_cast<uint8_t>(v >> 8);
    const uint8_t b2 = static_cast<uint8_t>(v >> 16);
    const uint8_t b3 = static_cast<uint8_t>(v >> 24);
    if (host_data_mono_) {
        HostDataMonoByte(b0);
        HostDataMonoByte(b1);
        HostDataMonoByte(b2);
        HostDataMonoByte(b3);
    } else {
        HostDataColorByte(b0);
        HostDataColorByte(b1);
        HostDataColorByte(b2);
        HostDataColorByte(b3);
    }
}
void SiemensMp377Sm501Blitter::ExecuteDdiFill(const SiemensMp377Sm501Blitter::State2d& st, bool use_pattern) {
    if (use_pattern && pattern_valid_)
        PatternFillRect16(st, st.fill_color);
    else
        FillRect16(st, st.fill_color);
}
uint32_t SiemensMp377Sm501Blitter::SurfaceWidthPixels16(const SiemensMp377Sm501Blitter::SurfaceState& s) const {
    if (s.base == 0u) return kFbWidth;
    if (s.pitch_pixels) return s.pitch_pixels;
    return s.pitch_bytes >= 2u ? s.pitch_bytes / 2u : kFbWidth;
}
uint32_t SiemensMp377Sm501Blitter::SurfaceHeightRows(const SiemensMp377Sm501Blitter::SurfaceState& s) const {
    if (s.base == 0u) return kFbHeight;
    if (s.base >= kSm501FbBytes || s.pitch_bytes == 0) return 0;
    return (kSm501FbBytes - s.base) / s.pitch_bytes;
}
uint16_t SiemensMp377Sm501Blitter::ApplyDdiRop16(uint8_t rop, uint16_t s, uint16_t d, uint16_t p) {
    uint16_t out = 0;
    for (unsigned bit = 0; bit < 16; ++bit) {
        const bool pb = ((p >> bit) & 1u) != 0;
        const bool sb = ((s >> bit) & 1u) != 0;
        const bool db = ((d >> bit) & 1u) != 0;
        if (Rop3Bit(rop, pb, sb, db)) out |= static_cast<uint16_t>(1u << bit);
    }
    return out;
}
SiemensMp377Sm501Blitter::VramBlitRect
SiemensMp377Sm501Blitter::NormalizeVramBlitRect(const SiemensMp377Sm501Blitter::State2d& st) const {
    SiemensMp377Sm501Blitter::VramBlitRect r;
    r.src_x = st.src_x;
    r.src_y = st.src_y;
    r.dst_x = st.dst_x;
    r.dst_y = st.dst_y;
    r.width = st.width;
    r.height = st.height;
    r.rtl_btl = st.backwards;
    if (r.rtl_btl) {
        if (r.width) {
            r.src_x = (r.src_x + 1u >= r.width) ? (r.src_x - r.width + 1u) : 0u;
            r.dst_x = (r.dst_x + 1u >= r.width) ? (r.dst_x - r.width + 1u) : 0u;
        }
        if (r.height) {
            r.src_y = (r.src_y + 1u >= r.height) ? (r.src_y - r.height + 1u) : 0u;
            r.dst_y = (r.dst_y + 1u >= r.height) ? (r.dst_y - r.height + 1u) : 0u;
        }
    }
    const uint32_t src_w = SurfaceWidthPixels16(st.src_surface);
    const uint32_t dst_w = SurfaceWidthPixels16(st.dst_surface);
    const uint32_t src_h = SurfaceHeightRows(st.src_surface);
    const uint32_t dst_h = SurfaceHeightRows(st.dst_surface);
    if (r.src_x >= src_w || r.src_y >= src_h || r.dst_x >= dst_w || r.dst_y >= dst_h) {
        r.width = 0;
        r.height = 0;
        return r;
    }
    r.width = std::min(r.width, std::min(src_w - r.src_x, dst_w - r.dst_x));
    r.height = std::min(r.height, std::min(src_h - r.src_y, dst_h - r.dst_y));
    return r;
}
void SiemensMp377Sm501Blitter::ExecuteDdiVideoToVideoChunk(const SiemensMp377Sm501Blitter::State2d& st,
                                                           const SiemensMp377Sm501Blitter::VramBlitRect& r,
                                                           uint32_t y_off, uint32_t rows) {
    auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
    if (!fb || rows == 0 || r.width == 0) return;
    uint8_t* vram = fb->MutableVramFor2d();
    if (!vram) return;
    const uint32_t src_stride = st.src_surface.pitch_bytes ? st.src_surface.pitch_bytes : st.src_pitch * 2u;
    const uint32_t dst_stride = st.dst_surface.pitch_bytes ? st.dst_surface.pitch_bytes : st.dst_pitch * 2u;
    const uint32_t dst_x = r.dst_x;
    std::vector<uint16_t> src_tmp(r.width * rows, 0u);
    for (uint32_t y = 0; y < rows; ++y) {
        const uint32_t src_row = st.src_surface.base + (r.src_y + y_off + y) * src_stride + r.src_x * 2u;
        for (uint32_t x = 0; x < r.width; ++x) {
            const uint32_t off = src_row + x * 2u;
            if (off + 1u < kSm501FbBytes)
                src_tmp[y * r.width + x] = static_cast<uint16_t>(vram[off] | (vram[off + 1u] << 8));
        }
    }
    for (uint32_t y = 0; y < rows; ++y) {
        const uint32_t dst_row = st.dst_surface.base + (r.dst_y + y_off + y) * dst_stride + dst_x * 2u;
        if (dst_row >= kSm501FbBytes) break;
        for (uint32_t x = 0; x < r.width; ++x) {
            const uint32_t off = dst_row + x * 2u;
            if (off + 1u >= kSm501FbBytes) break;
            const uint16_t src = src_tmp[y * r.width + x];
            const uint16_t d = static_cast<uint16_t>(vram[off] | (vram[off + 1u] << 8));
            const uint16_t o = ApplyDdiRop16(st.rop, src, d, st.fill_color);
            vram[off] = static_cast<uint8_t>(o);
            vram[off + 1u] = static_cast<uint8_t>(o >> 8);
        }
        fb->Note2dWrite(dst_row, r.width * 2u);
    }
}
void SiemensMp377Sm501Blitter::ExecuteDdiVideoToVideo(const SiemensMp377Sm501Blitter::State2d& st) {
    SiemensMp377Sm501Blitter::VramBlitRect r = NormalizeVramBlitRect(st);
    if (r.width == 0 || r.height == 0) {
        return;
    }
    constexpr uint32_t kDdiVramBlitMaxRows = 0xC0u;
    for (uint32_t y = 0; y < r.height;) {
        const uint32_t rows = std::min<uint32_t>(kDdiVramBlitMaxRows, r.height - y);
        ExecuteDdiVideoToVideoChunk(st, r, y, rows);
        y += rows;
    }
}
void SiemensMp377Sm501Blitter::ExecuteDdiVgx2dCommand(uint32_t cmd) {
    if (cmd == 0x40000000u) {
        BeginDdiPatternUpload();
        return;
    }
    FinishDdiPatternUpload();
    if (IsDdiVgxHostDataCommand(cmd)) {
        BeginDdiVgxHostDataCommand(cmd);
        return;
    }
    const SiemensMp377Sm501Blitter::State2d st = DecodeState2d(cmd);
    const uint32_t width = st.width;
    const uint32_t height = st.height;
    if (width == 0 || height == 0) {
        Log2dCommand(cmd, st, "zero-size");
        return;
    }
    const uint8_t rop = static_cast<uint8_t>(cmd & 0xFFu);
    if ((cmd & 0x40000000u) != 0) {
        Log2dCommand(cmd, st, "fill-pattern");
        ExecuteDdiFill(st, true);
    } else if ((cmd & 0x0C000000u) != 0 || RopDependsOnSource(rop)) {
        Log2dCommand(cmd, st, "blit");
        ExecuteDdiVideoToVideo(st);
    } else {
        Log2dCommand(cmd, st, "fill");
        ExecuteDdiFill(st, false);
    }
}
REGISTER_SERVICE(SiemensMp377Sm501Blitter);

} // namespace siemens_mp377
