#pragma once

#include <cstdint>

#include "imx6_vivante_blit_ops.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_surface_access.h"

namespace imx6_vivante {

class VivanteDraw2dState : protected VivanteSurfaceAccess {
protected:
    VivanteDraw2dState(VivanteState& s, VivanteMem& mem) : VivanteSurfaceAccess(mem), s_(s) {}

    void LoadState();
    DeCoord DstCoord(uint32_t x, uint32_t y) const;
    void ReadDstActual(uint32_t x, uint32_t y, uint32_t& argb, uint32_t& packed);
    void ReadDst(uint32_t x, uint32_t y, uint32_t& argb, uint32_t& packed);
    void WriteDst(uint32_t x, uint32_t y, uint32_t argb);
    bool FastSolidPatternFill(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h);
    void ClearDst(uint32_t x, uint32_t y);
    bool SourceTransparent(uint32_t packed) const;
    bool DestinationAccepts(uint32_t packed) const;
    bool PatternOpaque(uint32_t x, uint32_t y) const;
    bool PatternAccepts(uint32_t x, uint32_t y) const;
    bool ReadPatternMemory(uint32_t x, uint32_t y, uint32_t& argb);
    uint32_t PatternPixel(uint32_t x, uint32_t y);
    DeCoord SrcCoord(uint32_t x, uint32_t y) const;
    bool ReadSource(uint32_t x, uint32_t y, uint32_t& argb, uint32_t& packed);

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

}
