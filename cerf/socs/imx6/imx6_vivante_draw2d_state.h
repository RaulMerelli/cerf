#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "imx6_vivante_blit_ops.h"
#include "imx6_vivante_draw2d_regs.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_surface_access.h"

namespace imx6_vivante {

class VivanteDraw2dState : protected VivanteSurfaceAccess {
protected:
    VivanteDraw2dState(VivanteState& s, VivanteMem& mem)
        : VivanteSurfaceAccess(mem), s_(s) {}

    void LoadState() {
        pe20_ = mem_.Is2d();

        dst_addr_ = mem_.StateReg(kD2dDestAddress);
        dst_stride_ = mem_.StateReg(kD2dDestStride) & 0x3FFFFu;
        dst_cfg_ = mem_.StateReg(kD2dDestConfig);
        dst_fmt_ = dst_cfg_ & 0x1Fu;
        dst_tiled_ = (dst_cfg_ & (1u << 8)) != 0u;
        dst_minor_tiled_ = (dst_cfg_ & (1u << 26)) != 0u;
        dst_layout_ = dst_minor_tiled_
            ? SurfaceLayout::MinorTiled
            : (dst_tiled_ ? SurfaceLayout::Tiled : SurfaceLayout::Linear);
        dst_cmd_ = (dst_cfg_ >> 12) & 0xFu;
        gdi_stretch_ = (dst_cfg_ & (1u << 24)) != 0u;
        dst_swizzle_ = (dst_cfg_ >> 16) & 3u;
        dst_endian_ = (dst_cfg_ >> 20) & 3u;
        dst_bpp_ = BppFromDeFormat(dst_fmt_);
        dst_ptr_ = mem_.TranslateGpuToHostWrite(dst_addr_);

        dst_ready_ = dst_ptr_ && dst_stride_ != 0u && dst_bpp_ != 0u;

        src_addr_ = mem_.StateReg(kD2dSrcAddress);
        src_stride_ = mem_.StateReg(kD2dSrcStride) & 0x3FFFFu;
        src_cfg_ = mem_.StateReg(kD2dSrcConfig);
        src_ex_cfg_ = mem_.StateReg(kD2dSrcExConfig);
        src_ex_addr_ = mem_.StateReg(kD2dSrcExAddress);
        src_fmt_ = pe20_
            ? ((src_cfg_ >> 24) & 0x1Fu)
            : (src_cfg_ & 0xFu);
        src_swizzle_ = (src_cfg_ >> 20) & 3u;
        src_endian_ = (src_cfg_ >> 30) & 3u;
        src_relative_ = (src_cfg_ & (1u << 6)) != 0u;
        src_tiled_ = (src_cfg_ & (1u << 7)) != 0u;
        src_multi_tiled_ = (src_ex_cfg_ & 1u) != 0u;
        src_supertiled_ = (src_ex_cfg_ & (1u << 3)) != 0u;
        src_minor_tiled_ = (src_ex_cfg_ & (1u << 8)) != 0u;
        src_layout_conflict_ = src_minor_tiled_ &&
            (src_multi_tiled_ || src_supertiled_);
        src_layout_ = DecodeSurfaceLayout(
            src_tiled_, src_multi_tiled_, src_supertiled_, src_minor_tiled_);
        src_stream_ = (src_cfg_ & (1u << 8)) != 0u;
        src_pack_ = (src_cfg_ >> 12) & 3u;
        src_transparency_ = (src_cfg_ >> 4) & 3u;
        mono_transparent_one_ = (src_cfg_ & (1u << 15)) != 0u;
        src_bpp_ = BppFromDeFormat(src_fmt_);
        src_ptr_ = (src_addr_ && src_stride_ && src_bpp_)
            ? mem_.TranslateGpuToHost(src_addr_) : nullptr;
        src_extra_ptr_ = IsMultiLayout(src_layout_) && src_ex_addr_
            ? mem_.TranslateGpuToHost(src_ex_addr_) : nullptr;
        src_surface_configured_ = src_addr_ != 0u && src_stride_ != 0u &&
            src_bpp_ != 0u && !src_layout_conflict_ &&
            (!IsMultiLayout(src_layout_) || src_ex_addr_ != 0u);

        src_origin_ = mem_.StateReg(kD2dSrcOrigin);
        src_size_ = mem_.StateReg(kD2dSrcSize);
        if (src_stream_) {
            const uint32_t block_cfg = mem_.StateReg(kD2dMultiSrcConfig);
            const bool block_stream = (block_cfg & (1u << 8)) != 0u;
            const uint32_t block_fmt = pe20_ ? ((block_cfg >> 24) & 0x1Fu)
                                            : (block_cfg & 0xFu);
            const uint32_t block_pack = (block_cfg >> 12) & 3u;
            const uint32_t block_size = mem_.StateReg(kD2dMultiSrcSize);
            if (block_stream && block_fmt == 10u &&
                block_pack == src_pack_ && block_size != 0u) {
                src_origin_ = mem_.StateReg(kD2dMultiSrcOrigin);
                src_size_ = block_size;
            }
        }
        src_x0_ = Lo16(src_origin_);
        src_y0_ = Hi16(src_origin_);
        src_size_x_ = Lo16(src_size_);
        src_size_y_ = Hi16(src_size_);
        stretch_x_ = mem_.StateReg(kD2dStretchX) & 0x7FFFFFFFu;
        stretch_y_ = mem_.StateReg(kD2dStretchY) & 0x7FFFFFFFu;

        clear_argb_ = pe20_
            ? mem_.StateReg(kD2dClearPe20)
            : UnpackSurfaceColor(mem_.StateReg(kD2dClearPe10Lo), dst_fmt_);

        src_bg_ = mem_.StateReg(kD2dSrcColorBg);
        src_fg_ = mem_.StateReg(kD2dSrcColorFg);
        pat_bg_ = mem_.StateReg(kD2dPatternBg);
        pat_fg_ = mem_.StateReg(kD2dPatternFg);
        const uint32_t clip_low = mem_.StateReg(kD2dDestClipLow);
        const uint32_t clip_high = mem_.StateReg(kD2dDestClipHigh);
        clip_x0_ = Lo16(clip_low) & 0x7FFFu;
        clip_y0_ = Hi16(clip_low) & 0x7FFFu;
        clip_x1_ = Lo16(clip_high) & 0x7FFFu;
        clip_y1_ = Hi16(clip_high) & 0x7FFFu;
        rop_ = mem_.StateReg(kD2dRop);
        alpha_control_ = mem_.StateReg(kD2dAlphaControl);
        alpha_enable_ = (alpha_control_ & 1u) != 0u;
        alpha_modes_ = mem_.StateReg(kD2dAlphaModes);
        global_src_ = mem_.StateReg(kD2dGlobalSrcColor);
        global_dst_ = mem_.StateReg(kD2dGlobalDstColor);
        color_multiply_modes_ = mem_.StateReg(kD2dColorMultiplyModes);
        const uint32_t pe_transparency = mem_.StateReg(kD2dPeTransparency);
        if ((pe_transparency & 0x20000000u) != 0u) {
            /* etnaviv rnndb/state_2d.xml: PE_TRANSPARENCY bit 29 is
               DFB_COLOR_KEY.  The KTP400 taskbar traces contain no command
               stream case using it; fail honestly if a future batch does. */
            LOG(Trace, "Imx6Gpu%s: unsupported PE_TRANSPARENCY.DFB_COLOR_KEY "
                       "pe_trans=%08X", mem_.CoreName(), pe_transparency);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        pe_src_transparency_ = pe_transparency & 3u;
        pe_pat_transparency_ = (pe_transparency >> 4) & 3u;
        pe_dst_transparency_ = (pe_transparency >> 8) & 3u;
        effective_src_transparency_ =
            SelectSourceTransparency(pe20_, pe_src_transparency_,
                                     src_transparency_);
        use_src_override_ = (pe_transparency >> 16) & 3u;
        use_pat_override_ = (pe_transparency >> 20) & 3u;
        use_dst_override_ = (pe_transparency >> 24) & 3u;
        copy_like_ = RopCopyLike(rop_);
        rop_uses_source_ = RopUsesSource(rop_);
        rop_uses_pattern_ = RopUsesPattern(rop_);
        rop_uses_destination_ = RopUsesDestination(rop_);
        use_source_ = alpha_enable_ ? true :
            ResolveRopResource(rop_uses_source_, use_src_override_);
        use_pattern_ = alpha_enable_ ? false :
            ResolveRopResource(rop_uses_pattern_, use_pat_override_);
        use_destination_ =
            (alpha_enable_ || pe_dst_transparency_ == 2u) ? true :
            ResolveRopResource(rop_uses_destination_, use_dst_override_);
        pat_addr_ = mem_.StateReg(kD2dPatternAddress);
        pat_cfg_ = mem_.StateReg(kD2dPatternConfig);
        pat_low_ = mem_.StateReg(kD2dPatternLow);
        pat_high_ = mem_.StateReg(kD2dPatternHigh);
        pat_mask_low_ = mem_.StateReg(kD2dPatternMaskLow);
        pat_mask_high_ = mem_.StateReg(kD2dPatternMaskHigh);
        pat_fmt_ = pat_cfg_ & 0xFu;
        pat_memory_ = (pat_cfg_ & (1u << 4)) != 0u;
        pat_bpp_ = BppFromDeFormat(pat_fmt_);
        pat_ptr_ = (pat_memory_ && pat_addr_ && pat_bpp_)
            ? mem_.TranslateGpuToHost(pat_addr_) : nullptr;
        src_key_low_ = src_bg_;
        src_key_high_ = mem_.StateReg(kD2dSrcColorKeyHigh);
        dst_key_low_ = mem_.StateReg(kD2dDestColorKey);
        dst_key_high_ = mem_.StateReg(kD2dDestColorKeyHigh);

        src_rot_cfg_ = mem_.StateReg(kD2dSrcRotConfig);
        dst_rot_cfg_ = mem_.StateReg(kD2dDestRotConfig);
        rot_angle_ = mem_.StateReg(kD2dRotAngle);
        src_rot_ = ((rot_angle_ >> 8) & 1u)
            ? (rot_angle_ & 7u)
            : (((src_rot_cfg_ >> 16) & 1u) ? 4u : 0u);
        dst_rot_ = ((rot_angle_ >> 9) & 1u)
            ? ((rot_angle_ >> 3) & 7u)
            : (((dst_rot_cfg_ >> 16) & 1u) ? 4u : 0u);
        src_mirror_ = ((rot_angle_ >> 15) & 1u)
            ? ((rot_angle_ >> 12) & 3u) : 0u;
        dst_mirror_ = ((rot_angle_ >> 19) & 1u)
            ? ((rot_angle_ >> 16) & 3u) : 0u;
        src_surface_w_ = (src_rot_cfg_ & 0xFFFFu)
            ? (src_rot_cfg_ & 0xFFFFu)
            : ((src_stride_ && src_bpp_)
                   ? SurfaceWidthFromStride(src_stride_, src_bpp_, src_layout_)
                   : (src_size_x_ ? src_size_x_ : 0u));
        src_surface_h_ = (mem_.StateReg(kD2dSrcRotHeight) & 0xFFFFu)
            ? (mem_.StateReg(kD2dSrcRotHeight) & 0xFFFFu)
            : (src_size_y_ ? src_size_y_ : 0u);
        dst_surface_w_ = (dst_rot_cfg_ & 0xFFFFu)
            ? (dst_rot_cfg_ & 0xFFFFu)
            : ((dst_stride_ && dst_bpp_)
                   ? SurfaceWidthFromStride(dst_stride_, dst_bpp_, dst_layout_)
                   : 0u);
        dst_surface_h_ = mem_.StateReg(kD2dDestRotHeight) & 0xFFFFu;

        dst_mirror_rect_valid_ = false;
    }

    DeCoord DstCoord(uint32_t x, uint32_t y) const {
        if ((ValidDeRot(dst_rot_) == 0u && dst_mirror_ == 0u) ||
            dst_surface_w_ == 0u || dst_surface_h_ == 0u)
            return {x, y};

        /* KTP400 ddraw_ipu.dll sub_EF540980 reverses mono-blit bands
           around the complete destination rectangle before calling
           gco2D_MonoBlit when SetBitBlitMirror enables vertical mirror.
           Thus ROT0 mirroring reverses traversal inside each DRAW_2D
           rectangle; it does not relocate the rectangle across the
           destination surface.  Etnaviv rnndb/state_2d.xml defines the
           corresponding ROT_ANGLE DST_MIRROR field. */
        if ((dst_rot_ & 7u) == 0u && dst_mirror_rect_valid_) {
            DeCoord p{x, y};
            if ((dst_mirror_ & 1u) != 0u &&
                p.x >= dst_mirror_rect_x0_ && p.x < dst_mirror_rect_x1_) {
                p.x = dst_mirror_rect_x0_ +
                      (dst_mirror_rect_x1_ - 1u - p.x);
            }
            if ((dst_mirror_ & 2u) != 0u &&
                p.y >= dst_mirror_rect_y0_ && p.y < dst_mirror_rect_y1_) {
                p.y = dst_mirror_rect_y0_ +
                      (dst_mirror_rect_y1_ - 1u - p.y);
            }
            return p;
        }
        return TransformDeCoord(x, y, dst_surface_w_, dst_surface_h_,
                                dst_rot_, dst_mirror_);
    }

    bool ReadDstActual(uint32_t x, uint32_t y, uint32_t& argb,
                       uint32_t& packed) {
        const DeCoord p = DstCoord(x, y);
        if (!ReadSurfacePackedGpuLayout(dst_addr_, 0u, dst_stride_,
                                        p.x, p.y, dst_fmt_, dst_layout_,
                                        packed, false, dst_endian_,
                                        MmuClient::PixelEngine))
            return false;
        argb = UnpackSurfaceColor(packed, dst_fmt_, dst_swizzle_);
        return true;
    }

    bool ReadDst(uint32_t x, uint32_t y, uint32_t& argb, uint32_t& packed) {
        if (!use_destination_) {
            argb = 0xFF000000u;
            packed = PackSurfaceColor(argb, dst_fmt_, dst_swizzle_);
            return true;
        }
        return ReadDstActual(x, y, argb, packed);
    }

    bool WriteDst(uint32_t x, uint32_t y, uint32_t argb) {
        const DeCoord p = DstCoord(x, y);
        return WriteSurfacePixelGpuLayout(dst_addr_, 0u, dst_stride_,
                                          p.x, p.y, dst_fmt_, dst_swizzle_,
                                          dst_layout_, argb, false,
                                          dst_endian_);
    }

    bool FastSolidPatternFill(uint32_t x0, uint32_t y0,
                              uint32_t w, uint32_t h) {
        if (dst_layout_ != SurfaceLayout::Linear || dst_bpp_ != 4u ||
            dst_endian_ != 0u || dst_cmd_ != 2u || alpha_enable_ ||
            use_destination_ || pe_dst_transparency_ == 2u ||
            pe_pat_transparency_ == 1u || pat_ptr_ || (pat_cfg_ & (1u << 4)) ||
            ValidDeRot(dst_rot_) != 0u || (dst_mirror_ & 1u) != 0u)
            return false;

        const uint32_t pat_argb = PatternPixel(
            x0, y0, clear_argb_);
        const uint32_t out_argb = ApplyRop(rop_, 0u, pat_argb, pat_argb);
        const uint32_t packed = PackSurfaceColor(out_argb, dst_fmt_,
                                                 dst_swizzle_);
        std::vector<uint32_t> row(w, packed);
        for (uint32_t y = 0u; y < h; ++y) {
            const DeCoord p = DstCoord(x0, y0 + y);
            const size_t offset = static_cast<size_t>(p.y) * dst_stride_ +
                                  static_cast<size_t>(p.x) * dst_bpp_;
            uint32_t address = 0u;
            if (!AddGpuOffset(dst_addr_, offset, address) ||
                !mem_.WriteGpuBytes(address, row.data(),
                                    static_cast<size_t>(w) * dst_bpp_,
                                    MmuClient::PixelEngine))
                return false;
        }
        return true;
    }

    void ClearDst(uint32_t x, uint32_t y) {
        if (pe20_) {
            WriteDst(x, y, clear_argb_);
            return;
        }
        const DeCoord p = DstCoord(x, y);
        const size_t offset = LocateSurface(dst_stride_, p.x, p.y,
                                              dst_bpp_, dst_layout_).offset;
        ApplyPe10ClearGpu(dst_addr_, offset, dst_bpp_,
                          mem_.StateReg(kD2dClearPe10Lo),
                          mem_.StateReg(kD2dClearPe10Hi),
                          mem_.StateReg(kD2dClearByteMask) & 0xFFu,
                          dst_endian_);
    }

    bool SourceTransparent(uint32_t packed) const {
        return effective_src_transparency_ == 2u
            ? NativeColorKeyMatch(packed, src_key_low_, src_key_high_, src_fmt_)
            : false;
    }

    bool DestinationAccepts(uint32_t packed) const {
        if (pe_dst_transparency_ != 2u)
            return true;
        return NativeColorKeyMatch(packed, dst_key_low_, dst_key_high_, dst_fmt_);
    }

    bool PatternOpaque(uint32_t x, uint32_t y) const {
        if (pe_pat_transparency_ != 1u)
            return true;
        if ((pat_mask_low_ | pat_mask_high_) == 0u)
            return true;
        const uint32_t origin_x = (pat_cfg_ >> 16) & 7u;
        const uint32_t origin_y = (pat_cfg_ >> 20) & 7u;
        return ReadPatternBit(pat_mask_low_, pat_mask_high_,
                              x + origin_x, y + origin_y);
    }

    bool PatternAccepts(uint32_t x, uint32_t y) const {
        return PatternOpaque(x, y);
    }

    bool ReadPatternMemory(uint32_t x, uint32_t y, uint32_t& argb) {
        if ((!pat_ptr_ && !s_.de_pattern_latch_valid_) || pat_bpp_ == 0u)
            return false;

        /* etnaviv 2d.md + line2d_patterned.c: TYPE_PATTERN points at a
           repeated 8x8 source image.  PATTERN_ADDRESS has no companion
           stride register; the hardware consumes the initial 8x8 block
           tightly packed in PATTERN_CONFIG.FORMAT. */
        const uint32_t origin_x = (pat_cfg_ >> 16) & 7u;
        const uint32_t origin_y = (pat_cfg_ >> 20) & 7u;
        const uint32_t px = (x + origin_x) & 7u;
        const uint32_t py = (y + origin_y) & 7u;

        const bool use_latch = s_.de_pattern_latch_valid_ &&
            s_.de_pattern_latch_config_ == pat_cfg_ &&
            s_.de_pattern_latch_address_ == pat_addr_ &&
            s_.de_pattern_latch_bpp_ == pat_bpp_;
        const size_t latch_offset =
            (static_cast<size_t>(py) * 8u + px) * pat_bpp_;

        if ((pat_fmt_ & 0x1Fu) == 9u) { /* PE20 INDEX8 pattern brush. */
            uint32_t ix = 0u;
            if (use_latch) {
                ix = s_.de_pattern_latch_[latch_offset];
            } else if (!ReadPackedGpu(pat_addr_, py * 8u + px, 1u, 0u, ix)) {
                return false;
            }
            argb = mem_.StateReg(kD2dIndexColorTable32 + (ix & 0xFFu) * 4u);
            return true;
        }

        if ((pat_fmt_ & 0x1Fu) == 16u) { /* A8 brush: alpha over PATTERN_FG RGB. */
            uint32_t a = 0u;
            if (use_latch) {
                a = s_.de_pattern_latch_[latch_offset];
            } else if (!ReadPackedGpu(pat_addr_, py * 8u + px, 1u, 0u, a)) {
                return false;
            }
            argb = ((a & 0xFFu) << 24) |
                   (NormalizeArgb(pat_fg_) & 0x00FFFFFFu);
            return true;
        }

        if (use_latch && pat_bpp_ <= 4u) {
            uint32_t packed = 0u;
            std::memcpy(&packed, s_.de_pattern_latch_ + latch_offset, pat_bpp_);
            argb = UnpackSurfaceColor(packed, pat_fmt_, 0u);
            return true;
        }

        return ReadSurfacePixelGpu(pat_addr_, 8u * pat_bpp_, px, py,
                                   pat_fmt_, 0u, false, false, argb);
    }

    uint32_t PatternPixel(uint32_t x, uint32_t y, uint32_t fallback) {
        uint32_t argb = 0u;
        if (ReadPatternMemory(x, y, argb))
            return argb;
        return VivanteBlitOps::PatternPixel(x, y, pat_cfg_, pat_low_, pat_high_,
                                            pat_bg_, pat_fg_, fallback);
    }

    DeCoord SrcCoord(uint32_t x, uint32_t y) const {
        if ((ValidDeRot(src_rot_) == 0u && src_mirror_ == 0u) ||
            src_surface_w_ == 0u || src_surface_h_ == 0u)
            return {x, y};
        return TransformDeCoord(x, y, src_surface_w_, src_surface_h_,
                                src_rot_, src_mirror_);
    }

    bool ReadSource(uint32_t x, uint32_t y, uint32_t& argb, uint32_t& packed) {
        const DeCoord sp = SrcCoord(x, y);
        if (!src_surface_configured_)
            return false;
        if (!ReadSurfacePackedGpuLayout(src_addr_, src_ex_addr_, src_stride_,
                                        sp.x, sp.y, src_fmt_, src_layout_,
                                        packed, false, src_endian_))
            return false;

        if ((src_fmt_ & 0x1Fu) == 16u) { /* PE20 A8: alpha mask + GLOBAL_SRC_COLOR RGB. */
            argb = ((packed & 0xFFu) << 24) | (global_src_ & 0x00FFFFFFu);
            return true;
        }

        if ((src_fmt_ & 0x1Fu) == 9u) { /* PE20 INDEX8 via INDEX_COLOR_TABLE32. */
            argb = mem_.StateReg(kD2dIndexColorTable32 + (packed & 0xFFu) * 4u);
            return true;
        }

        argb = UnpackSurfaceColor(packed, src_fmt_, src_swizzle_);
        return true;
    }

    VivanteState& s_;

    bool pe20_ = false;
    uint32_t dst_addr_ = 0u, dst_stride_ = 0u, dst_cfg_ = 0u, dst_fmt_ = 0u;
    bool dst_tiled_ = false, dst_minor_tiled_ = false;
    SurfaceLayout dst_layout_ = SurfaceLayout::Linear;
    uint32_t dst_cmd_ = 0u;
    bool gdi_stretch_ = false;
    uint32_t dst_swizzle_ = 0u, dst_endian_ = 0u, dst_bpp_ = 0u;
    uint8_t* dst_ptr_ = nullptr;
    bool dst_ready_ = false;

    uint32_t src_addr_ = 0u, src_stride_ = 0u, src_cfg_ = 0u, src_ex_cfg_ = 0u;
    uint32_t src_ex_addr_ = 0u, src_fmt_ = 0u, src_swizzle_ = 0u, src_endian_ = 0u;
    bool src_relative_ = false, src_tiled_ = false, src_multi_tiled_ = false;
    bool src_supertiled_ = false, src_minor_tiled_ = false, src_layout_conflict_ = false;
    SurfaceLayout src_layout_ = SurfaceLayout::Linear;
    bool src_stream_ = false;
    uint32_t src_pack_ = 0u, src_transparency_ = 0u;
    bool mono_transparent_one_ = false;
    uint32_t src_bpp_ = 0u;
    const uint8_t* src_ptr_ = nullptr;
    const uint8_t* src_extra_ptr_ = nullptr;
    bool src_surface_configured_ = false;

    uint32_t src_origin_ = 0u, src_size_ = 0u;
    uint32_t src_x0_ = 0u, src_y0_ = 0u, src_size_x_ = 0u, src_size_y_ = 0u;
    uint32_t stretch_x_ = 0u, stretch_y_ = 0u;

    uint32_t clear_argb_ = 0u;
    uint32_t src_bg_ = 0u, src_fg_ = 0u, pat_bg_ = 0u, pat_fg_ = 0u;
    uint32_t clip_x0_ = 0u, clip_y0_ = 0u, clip_x1_ = 0u, clip_y1_ = 0u;
    uint32_t rop_ = 0u;
    uint32_t alpha_control_ = 0u;
    bool alpha_enable_ = false;
    uint32_t alpha_modes_ = 0u, global_src_ = 0u, global_dst_ = 0u;
    uint32_t color_multiply_modes_ = 0u;
    uint32_t pe_src_transparency_ = 0u, pe_pat_transparency_ = 0u, pe_dst_transparency_ = 0u;
    uint32_t effective_src_transparency_ = 0u;
    uint32_t use_src_override_ = 0u, use_pat_override_ = 0u, use_dst_override_ = 0u;
    bool copy_like_ = false, rop_uses_source_ = false, rop_uses_pattern_ = false;
    bool rop_uses_destination_ = false;
    bool use_source_ = false, use_pattern_ = false, use_destination_ = false;
    uint32_t pat_addr_ = 0u, pat_cfg_ = 0u, pat_low_ = 0u, pat_high_ = 0u;
    uint32_t pat_mask_low_ = 0u, pat_mask_high_ = 0u, pat_fmt_ = 0u;
    bool pat_memory_ = false;
    uint32_t pat_bpp_ = 0u;
    const uint8_t* pat_ptr_ = nullptr;
    uint32_t src_key_low_ = 0u, src_key_high_ = 0u, dst_key_low_ = 0u, dst_key_high_ = 0u;

    uint32_t src_rot_cfg_ = 0u, dst_rot_cfg_ = 0u, rot_angle_ = 0u;
    uint32_t src_rot_ = 0u, dst_rot_ = 0u, src_mirror_ = 0u, dst_mirror_ = 0u;
    uint32_t src_surface_w_ = 0u, src_surface_h_ = 0u;
    uint32_t dst_surface_w_ = 0u, dst_surface_h_ = 0u;

    uint32_t dst_mirror_rect_x0_ = 0u, dst_mirror_rect_y0_ = 0u;
    uint32_t dst_mirror_rect_x1_ = 0u, dst_mirror_rect_y1_ = 0u;
    bool dst_mirror_rect_valid_ = false;
};

}  // namespace imx6_vivante
