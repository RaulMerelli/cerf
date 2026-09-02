#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "../../core/cerf_emulator.h"
#include "imx6_vivante_blit_ops.h"
#include "imx6_vivante_draw2d_multisource.h"
#include "imx6_vivante_draw2d_regs.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_rs.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_vr.h"

namespace imx6_vivante {

class VivanteDraw2d : protected VivanteDraw2dMultiSource {
public:
    VivanteDraw2d(VivanteState& s, VivanteMem& mem, CerfEmulator&) : VivanteDraw2dMultiSource(s, mem) {}

    void Execute(const uint32_t* rect_words, uint32_t rect_count, const uint32_t* stream_data, uint32_t stream_words) {
        LoadState();
        if (!dst_ready_) return;

        const uint8_t* stream = stream_words ? reinterpret_cast<const uint8_t*>(stream_data) : nullptr;

        /* etnaviv doc/2d.md: LOCATION_STREAM mono expansion accepts exactly
           one rectangle. */
        const uint32_t render_rect_count = (src_stream_ && rect_count > 1u) ? 1u : rect_count;
        for (uint32_t i = 0; i < render_rect_count; ++i) {
            dst_mirror_rect_valid_ = false;
            if (!rect_words) break;
            const uint32_t rw[2] = {
                rect_words[i * 2u],
                rect_words[i * 2u + 1u],
            };
            uint32_t x0 = Lo16(rw[0]), y0 = Hi16(rw[0]);
            uint32_t x1 = Lo16(rw[1]), y1 = Hi16(rw[1]);
            const uint32_t rect_x0 = x0, rect_y0 = y0;
            const uint32_t rect_x1 = x1, rect_y1 = y1;

            if (dst_cmd_ == 1u) {
                ExecuteLine(rect_x0, rect_y0, rect_x1, rect_y1);
                continue;
            }

            const uint32_t rect_w = (rect_x1 > rect_x0) ? (rect_x1 - rect_x0) : 0u;
            const uint32_t rect_h = (rect_y1 > rect_y0) ? (rect_y1 - rect_y0) : 0u;
            if (rect_w != 0u && rect_h != 0u) {
                dst_mirror_rect_x0_ = rect_x0;
                dst_mirror_rect_y0_ = rect_y0;
                dst_mirror_rect_x1_ = rect_x1;
                dst_mirror_rect_y1_ = rect_y1;
                dst_mirror_rect_valid_ = true;
            }
            /* Clipping is always enabled for DRAW_2D.  An empty clip
               rectangle therefore rejects the draw; it does not disable clip. */
            x0 = (x0 > clip_x0_) ? x0 : clip_x0_;
            y0 = (y0 > clip_y0_) ? y0 : clip_y0_;
            x1 = (x1 < clip_x1_) ? x1 : clip_x1_;
            y1 = (y1 < clip_y1_) ? y1 : clip_y1_;
            if (clip_x1_ <= clip_x0_ || clip_y1_ <= clip_y0_ || x1 <= x0 || y1 <= y0) continue;
            const uint32_t w = x1 - x0;
            const uint32_t h = y1 - y0;

            if (dst_cmd_ == 0u) {
                for (uint32_t y = 0; y < h; ++y)
                    for (uint32_t x = 0; x < w; ++x)
                        ClearDst(x0 + x, y0 + y);
                continue;
            }

            if (dst_cmd_ == 8u) {
                ExecuteMultiSourceRect(rect_x0, rect_y0, x0, y0, w, h);
                continue;
            }

            if ((dst_cmd_ == 2u || dst_cmd_ == 3u || dst_cmd_ == 4u) && src_stream_ && stream) {
                ExecuteMonoStream(stream, stream_words, rect_x0, rect_y0, rect_w, rect_h, x0, y0, w, h);
                continue;
            }

            if ((dst_cmd_ == 2u || dst_cmd_ == 3u || dst_cmd_ == 4u) && src_surface_configured_ &&
                (copy_like_ || use_source_)) {
                ExecuteCopyLike(rect_x0, rect_y0, rect_w, rect_h, x0, y0, w, h);
                continue;
            }

            if ((dst_cmd_ == 2u || dst_cmd_ == 3u || dst_cmd_ == 4u) &&
                (use_pattern_ || !use_source_ || src_addr_ == 0u)) {
                ExecutePatternFill(x0, y0, w, h);
                continue;
            }
        }
    }

private:
    void ExecuteLine(uint32_t rect_x0, uint32_t rect_y0, uint32_t rect_x1, uint32_t rect_y1) {
        RasterizeLineBresenhamExclusive(rect_x0, rect_y0, rect_x1, rect_y1, [&](uint32_t px, uint32_t py) {
            if (clip_x1_ <= clip_x0_ || clip_y1_ <= clip_y0_ || px < clip_x0_ || px >= clip_x1_ || py < clip_y0_ ||
                py >= clip_y1_)
                return;

            uint32_t dst_argb = 0u;
            uint32_t dst_packed = 0u;
            if (!ReadDst(px, py, dst_argb, dst_packed) || !DestinationAccepts(dst_packed) || !PatternAccepts(px, py))
                return;

            const uint32_t pat_argb = PatternPixel(px, py, clear_argb_);
            WriteDst(px, py, ApplyRop(rop_, dst_argb, src_fg_, pat_argb));
        });
    }

    void ExecuteMonoStream(const uint8_t* stream, uint32_t stream_words, uint32_t rect_x0, uint32_t rect_y0,
                           uint32_t rect_w, uint32_t rect_h, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
        const uint32_t base_src_w = src_size_x_ ? src_size_x_ : w;
        const uint32_t base_src_h = src_size_y_ ? src_size_y_ : h;
        uint32_t stream_width = rect_w ? rect_w : w;
        uint32_t stream_height = rect_h ? rect_h : h;
        if (!ResolveMonoStreamExtent(src_x0_, src_relative_, stream_width, stream_width) ||
            !ResolveMonoStreamExtent(src_y0_, src_relative_, stream_height, stream_height)) {
            return;
        }
        for (uint32_t yi = 0; yi < h; ++yi) {
            const uint32_t y = (dst_cmd_ == 3u) ? (h - 1u - yi) : yi;
            const uint32_t dy = (y0 + y) - rect_y0;
            const uint32_t source_y_offset =
                dst_cmd_ == 4u ? StretchCoordinate(dy, base_src_h, rect_h, stretch_y_, gdi_stretch_) : dy;
            for (uint32_t xi = 0; xi < w; ++xi) {
                const uint32_t x = (dst_cmd_ == 3u) ? (w - 1u - xi) : xi;
                const uint32_t dx = (x0 + x) - rect_x0;
                uint32_t stream_x = 0u;
                uint32_t stream_y = 0u;
                if (!ResolveMonoStreamCoordinate(src_x0_, src_relative_, x0 + x, dx, stream_x) ||
                    !ResolveMonoStreamCoordinate(src_y0_, src_relative_, y0 + y, dy, stream_y)) {
                    continue;
                }
                const bool one = VivanteBlitOps::ReadStreamMonoBit(stream, stream_x, stream_y, stream_width, src_pack_,
                                                                   stream_words) != 0u;
                const bool mono_masked = effective_src_transparency_ == 1u;
                const bool mono_transparent = mono_masked;
                const MonoRopSelection mono_rop = SelectMonoRop(one, mono_transparent, mono_transparent_one_);
                if (!mono_rop.write) continue;
                const bool pixel_use_source =
                    alpha_enable_
                        ? true
                        : ResolveRopResource(RopBranchUsesSource(rop_, mono_rop.foreground), use_src_override_);
                const bool pixel_use_pattern =
                    alpha_enable_
                        ? false
                        : ResolveRopResource(RopBranchUsesPattern(rop_, mono_rop.foreground), use_pat_override_);
                const bool pixel_use_destination =
                    (alpha_enable_ || pe_dst_transparency_ == 2u)
                        ? true
                        : ResolveRopResource(RopBranchUsesDestination(rop_, mono_rop.foreground), use_dst_override_);

                uint32_t src_argb = one ? src_fg_ : src_bg_;
                if (mono_masked && pixel_use_source && src_surface_configured_) {
                    const uint32_t source_x_offset =
                        dst_cmd_ == 4u ? StretchCoordinate(dx, base_src_w, rect_w, stretch_x_, gdi_stretch_) : dx;
                    uint32_t sx = 0u;
                    uint32_t sy = 0u;
                    if (!ResolveSourceCoordinate(src_x0_, src_relative_, x0 + x, source_x_offset, sx) ||
                        !ResolveSourceCoordinate(src_y0_, src_relative_, y0 + y, source_y_offset, sy))
                        continue;
                    uint32_t src_packed = 0u;
                    if (!ReadSource(sx, sy, src_argb, src_packed)) continue;
                }
                uint32_t dst_argb = 0u;
                uint32_t dst_packed = 0u;
                if (pixel_use_destination) {
                    if (!ReadDstActual(x0 + x, y0 + y, dst_argb, dst_packed)) continue;
                } else {
                    dst_argb = 0xFF000000u;
                    dst_packed = PackSurfaceColor(dst_argb, dst_fmt_, dst_swizzle_);
                }
                if (!DestinationAccepts(dst_packed)) continue;
                if (pixel_use_pattern && !PatternAccepts(x0 + x, y0 + y)) continue;
                const uint32_t pat_argb = pixel_use_pattern ? PatternPixel(x0 + x, y0 + y, clear_argb_) : 0u;
                WriteDst(x0 + x, y0 + y, ApplyRop(rop_, dst_argb, src_argb, pat_argb, mono_rop.foreground));
            }
        }
    }

    void ExecuteCopyLike(uint32_t rect_x0, uint32_t rect_y0, uint32_t rect_w, uint32_t rect_h, uint32_t x0, uint32_t y0,
                         uint32_t w, uint32_t h) {
        const uint32_t base_src_w = src_size_x_ ? src_size_x_ : w;
        const uint32_t base_src_h = src_size_y_ ? src_size_y_ : h;
        auto sourceCoordinates = [&](uint32_t x, uint32_t y, uint32_t& sx, uint32_t& sy) -> bool {
            const uint32_t dx = (x0 + x) - rect_x0;
            const uint32_t dy = (y0 + y) - rect_y0;
            const uint32_t source_x_offset =
                dst_cmd_ == 4u ? StretchCoordinate(dx, base_src_w, rect_w, stretch_x_, gdi_stretch_) : dx;
            const uint32_t source_y_offset =
                dst_cmd_ == 4u ? StretchCoordinate(dy, base_src_h, rect_h, stretch_y_, gdi_stretch_) : dy;
            return ResolveSourceCoordinate(src_x0_, src_relative_, x0 + x, source_x_offset, sx) &&
                   ResolveSourceCoordinate(src_y0_, src_relative_, y0 + y, source_y_offset, sy);
        };

        struct SourceSample {
            uint32_t argb = 0u;
            uint32_t packed = 0u;
            bool valid = false;
        };
        std::vector<SourceSample> source_snapshot;
        bool use_source_snapshot = false;

        /* A normal BIT_BLT is allowed to copy within one allocation; detect
           aliasing from the actual GPU byte ranges touched by both surfaces
           and acquire the complete source rectangle before the first write. */
        if (dst_cmd_ != 4u) {
            uintptr_t src_begin = ~static_cast<uintptr_t>(0u);
            uintptr_t src_end = 0u;
            uintptr_t dst_begin = ~static_cast<uintptr_t>(0u);
            uintptr_t dst_end = 0u;
            bool src_range_valid = false;
            bool dst_range_valid = false;

            auto includeGpuRange = [&](uint32_t base, size_t offset, uint32_t bytes, uint32_t endian,
                                       uintptr_t& range_begin, uintptr_t& range_end, bool& range_valid) {
                for (uint32_t byte = 0u; byte < bytes; ++byte) {
                    const size_t mapped_offset = EndianByteOffset(offset + byte, endian);
                    uint32_t address = 0u;
                    if (!AddGpuOffset(base, mapped_offset, address)) continue;
                    const uintptr_t begin = address;
                    const uintptr_t end = begin + 1u;
                    if (!range_valid || begin < range_begin) range_begin = begin;
                    if (!range_valid || end > range_end) range_end = end;
                    range_valid = true;
                }
            };

            for (uint32_t py = 0u; py < h; ++py) {
                for (uint32_t px = 0u; px < w; ++px) {
                    uint32_t sx = 0u, sy = 0u;
                    if (!sourceCoordinates(px, py, sx, sy)) continue;
                    const DeCoord sp = SrcCoord(sx, sy);
                    const SurfaceLocation sl = LocateSurface(src_stride_, sp.x, sp.y, src_bpp_, src_layout_);
                    const uint32_t source_base = sl.plane == 0u ? src_addr_ : src_ex_addr_;
                    includeGpuRange(source_base, sl.offset, src_bpp_, src_endian_, src_begin, src_end, src_range_valid);

                    const DeCoord dp = DstCoord(x0 + px, y0 + py);
                    const SurfaceLocation dl = LocateSurface(dst_stride_, dp.x, dp.y, dst_bpp_, dst_layout_);
                    includeGpuRange(dst_addr_, dl.offset, dst_bpp_, dst_endian_, dst_begin, dst_end, dst_range_valid);
                }
            }

            const bool base_host_alias =
                src_ptr_ == dst_ptr_ || (src_extra_ptr_ != nullptr && src_extra_ptr_ == dst_ptr_);
            use_source_snapshot = src_range_valid && dst_range_valid &&
                                  (AddressRangesOverlap(src_begin, src_end, dst_begin, dst_end) || base_host_alias);
            if (use_source_snapshot) {
                source_snapshot.resize(static_cast<size_t>(w) * h);
                for (uint32_t py = 0u; py < h; ++py) {
                    for (uint32_t px = 0u; px < w; ++px) {
                        uint32_t sx = 0u, sy = 0u;
                        const bool coordinate_valid = sourceCoordinates(px, py, sx, sy);
                        SourceSample& sample = source_snapshot[static_cast<size_t>(py) * w + px];
                        sample.valid = coordinate_valid && ReadSource(sx, sy, sample.argb, sample.packed);
                    }
                }
            }
        }

        for (uint32_t yi = 0; yi < h; ++yi) {
            const uint32_t y = (dst_cmd_ == 3u) ? (h - 1u - yi) : yi;
            for (uint32_t xi = 0; xi < w; ++xi) {
                const uint32_t x = (dst_cmd_ == 3u) ? (w - 1u - xi) : xi;
                uint32_t src_argb = 0u;
                uint32_t src_packed = 0u;
                bool source_valid = false;
                if (use_source_snapshot) {
                    const SourceSample& sample = source_snapshot[static_cast<size_t>(y) * w + x];
                    src_argb = sample.argb;
                    src_packed = sample.packed;
                    source_valid = sample.valid;
                } else {
                    uint32_t sx = 0u, sy = 0u;
                    source_valid = sourceCoordinates(x, y, sx, sy) && ReadSource(sx, sy, src_argb, src_packed);
                }
                if (source_valid) {
                    uint32_t dst_argb = 0u;
                    uint32_t dst_packed = 0u;
                    if (!ReadDst(x0 + x, y0 + y, dst_argb, dst_packed)) continue;
                    if (!DestinationAccepts(dst_packed)) continue;
                    /* etnaviv doc/2d.md ROP4: ROP_FG for opaque pixels, ROP_BG
                       for transparent - a transparent pixel is not necessarily
                       a skipped write. */
                    const bool source_transparent = SourceTransparent(src_packed);
                    const bool pattern_opaque = PatternOpaque(x0 + x, y0 + y);
                    const bool transparent = source_transparent || !pattern_opaque;
                    if (transparent && ((rop_ >> 20) & 3u) != 3u) continue;
                    const uint32_t pat_argb = PatternPixel(x0 + x, y0 + y, clear_argb_);
                    const uint32_t out_argb = alpha_enable_
                                                  ? BlendPePixel(src_argb, dst_argb, alpha_control_, alpha_modes_,
                                                                 color_multiply_modes_, global_src_, global_dst_, pe20_)
                                                  : ApplyRop(rop_, dst_argb, src_argb, pat_argb, !transparent);
                    WriteDst(x0 + x, y0 + y, out_argb);
                }
            }
        }
    }

    void ExecutePatternFill(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
        if (FastSolidPatternFill(x0, y0, w, h)) return;
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                uint32_t dst_argb = 0u;
                uint32_t dst_packed = 0u;
                if (!ReadDst(x0 + x, y0 + y, dst_argb, dst_packed)) continue;
                if (!DestinationAccepts(dst_packed)) continue;
                if (!PatternAccepts(x0 + x, y0 + y)) continue;
                const uint32_t pat_argb = PatternPixel(x0 + x, y0 + y, clear_argb_);
                WriteDst(x0 + x, y0 + y, ApplyRop(rop_, dst_argb, pat_argb, pat_argb));
            }
        }
    }
};

class VivanteBlit {
public:
    VivanteBlit(VivanteState& s, VivanteMem& mem, CerfEmulator& emu) : draw2d_(s, mem, emu), vr_(s, mem), rs_(s, mem) {}

    void ExecuteDraw2d(const uint32_t* rect_words, uint32_t rect_count, const uint32_t* stream_data,
                       uint32_t stream_words) {
        draw2d_.Execute(rect_words, rect_count, stream_data, stream_words);
    }

    void ExecuteVideoRasterizer(uint32_t start_value) { vr_.Execute(start_value); }

    void ExecuteRs() { rs_.ExecuteRs(); }
    void ExecuteRsInPlace(uint32_t tile_count) { rs_.ExecuteRsInPlace(tile_count); }

private:
    VivanteDraw2d draw2d_;
    VivanteVr vr_;
    VivanteRs rs_;
};

} // namespace imx6_vivante
