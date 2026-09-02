#pragma once

#include <cstdint>

#include "../../core/log.h"
#include "imx6_vivante_blit_ops.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_surface_access.h"

namespace imx6_vivante {

class VivanteRs : protected VivanteSurfaceAccess {
public:
    VivanteRs(VivanteState& s, VivanteMem& mem) : VivanteSurfaceAccess(mem), s_(s) {}

    void ExecuteRsInPlace(uint32_t tile_count) {
        /* etnaviv rnndb/state_3d.xml documents RS.KICKER_INPLACE as GC3000
           and newer.  None of the i.MX6 DualLite cores modelled here
           advertises that engine, so this write must not run a normal
           source-to-destination resolve with stale addresses. */
        if (tile_count != 0u) {
            mem_.HaltUnsupported("imx6-vivante unsupported RS.KICKER_INPLACE", 0x016B0u, tile_count);
        }
    }

    void ExecuteRs() {
        /* RS offsets and field definitions: etnaviv rnndb/state_3d.xml.
           WINDOW_SIZE is the source size for a copy/downsample and is the
           per-pipe window on a two-pipe engine. */
        constexpr uint32_t kRsConfig = 0x01604u;
        constexpr uint32_t kRsSourceAddr = 0x01608u;
        constexpr uint32_t kRsSourceStride = 0x0160Cu;
        constexpr uint32_t kRsDestAddr = 0x01610u;
        constexpr uint32_t kRsDestStride = 0x01614u;
        constexpr uint32_t kRsWindowSize = 0x01620u;
        constexpr uint32_t kRsClearControl = 0x0163Cu;
        constexpr uint32_t kRsFillValue0 = 0x01640u;
        constexpr uint32_t kRsPipeSourceAddr = 0x016C0u;
        constexpr uint32_t kRsPipeDestAddr = 0x016E0u;
        constexpr uint32_t kRsPipeOffset = 0x01700u;
        constexpr uint32_t kTsMemConfig = 0x01654u;
        constexpr uint32_t kTsColorStatus = 0x01658u;
        constexpr uint32_t kTsColorSurface = 0x0165Cu;
        constexpr uint32_t kTsColorClear = 0x01660u;

        const uint32_t cfg = mem_.StateReg(kRsConfig);
        const uint32_t src_fmt = cfg & 0x1Fu;
        const uint32_t dst_fmt = (cfg >> 8) & 0x1Fu;
        const bool downsample_x = (cfg & (1u << 5)) != 0u;
        const bool downsample_y = (cfg & (1u << 6)) != 0u;
        const bool src_cfg_tiled = (cfg & (1u << 7)) != 0u;
        const bool dst_cfg_tiled = (cfg & (1u << 14)) != 0u;
        const bool swap_rb = (cfg & (1u << 29)) != 0u;
        const bool flip_y = (cfg & (1u << 30)) != 0u;

        const uint32_t src_addr = mem_.StateReg(kRsSourceAddr);
        const uint32_t src_stride_reg = mem_.StateReg(kRsSourceStride);
        const uint32_t src_stride = src_stride_reg & 0x3FFFFu;
        const uint32_t dst_addr = mem_.StateReg(kRsDestAddr);
        const uint32_t dst_stride_reg = mem_.StateReg(kRsDestStride);
        const uint32_t dst_stride = dst_stride_reg & 0x3FFFFu;
        const uint32_t wh = mem_.StateReg(kRsWindowSize);
        const uint32_t window_w = Lo16(wh);
        const uint32_t window_h = Hi16(wh);
        const uint32_t clear_control = mem_.StateReg(kRsClearControl);
        const uint32_t clear_bits = clear_control & 0xFFFFu;
        const uint32_t clear_mode = (clear_control >> 16) & 3u;
        const uint32_t src_bpp = BppFromDeFormat(src_fmt);
        const uint32_t dst_bpp = BppFromDeFormat(dst_fmt);

        if (!IsKnownDeFormat(dst_fmt) || dst_bpp == 0u) {
            mem_.HaltUnsupported("imx6-vivante unsupported RS destination format", kRsConfig, dst_fmt);
        }
        if (clear_mode == 0u && (!IsKnownDeFormat(src_fmt) || src_bpp == 0u)) {
            mem_.HaltUnsupported("imx6-vivante unsupported RS source format", kRsConfig, src_fmt);
        }

        /* CONFIG.TILED chooses 4x4 fine tiling.  STRIDE.TILING promotes it to
           the classic 64x64 supertile layout and SUPER_TILED_NEW selects the
           newer Morton-order supertile. */
        const bool src_supertiled_new = (src_stride_reg & (1u << 27)) != 0u;
        const bool dst_supertiled_new = (dst_stride_reg & (1u << 27)) != 0u;
        const bool src_supertiled = (src_stride_reg & (1u << 31)) != 0u || src_supertiled_new;
        const bool dst_supertiled = (dst_stride_reg & (1u << 31)) != 0u || dst_supertiled_new;
        const bool src_tiled = src_cfg_tiled || src_supertiled;
        const bool dst_tiled = dst_cfg_tiled || dst_supertiled;
        const bool src_multi = (src_stride_reg & (1u << 30)) != 0u;
        const bool dst_multi = (dst_stride_reg & (1u << 30)) != 0u;

        if (dst_stride == 0u || window_w == 0u || window_h == 0u || (clear_mode == 0u && src_stride == 0u)) {
            return;
        }

        if ((src_multi || dst_multi) && mem_.PixelPipes() < 2u) {
            mem_.HaltUnsupported("imx6-vivante RS MULTI requested on a one-pipe core", src_stride_reg, dst_stride_reg);
        }

        uint32_t pipe_src_addr[2] = {mem_.StateReg(kRsPipeSourceAddr), mem_.StateReg(kRsPipeSourceAddr + 4u)};
        uint32_t pipe_dst_addr[2] = {mem_.StateReg(kRsPipeDestAddr), mem_.StateReg(kRsPipeDestAddr + 4u)};
        const uint32_t pipe_offset_reg[2] = {mem_.StateReg(kRsPipeOffset), mem_.StateReg(kRsPipeOffset + 4u)};
        const uint32_t pipe_offset_x[2] = {Lo16(pipe_offset_reg[0]), Lo16(pipe_offset_reg[1])};
        const uint32_t pipe_offset_y[2] = {Hi16(pipe_offset_reg[0]), Hi16(pipe_offset_reg[1])};

        uint32_t pipe_count = 1u;
        if (mem_.PixelPipes() >= 2u &&
            (src_multi || dst_multi || pipe_src_addr[1] != 0u || pipe_dst_addr[1] != 0u || pipe_offset_reg[1] != 0u)) {
            pipe_count = 2u;
        }

        /* Two-pipe GALCore/Etnaviv submissions program PIPE_SOURCE_ADDR[0]
           and PIPE_DEST_ADDR[0] even for a non-MULTI surface.  Prefer those
           addresses only when the command is actually using the two-pipe
           register set; otherwise retain the ordinary RS addresses. */
        const uint32_t common_src_addr = (pipe_count > 1u && pipe_src_addr[0] != 0u) ? pipe_src_addr[0] : src_addr;
        const uint32_t common_dst_addr = (pipe_count > 1u && pipe_dst_addr[0] != 0u) ? pipe_dst_addr[0] : dst_addr;
        if (!src_multi) {
            pipe_src_addr[0] = common_src_addr;
            pipe_src_addr[1] = common_src_addr;
        }
        if (!dst_multi) {
            pipe_dst_addr[0] = common_dst_addr;
            pipe_dst_addr[1] = common_dst_addr;
        }

        for (uint32_t p = 0; p < pipe_count; ++p) {
            if (clear_mode == 0u) {
                if (pipe_src_addr[p] == 0u || !mem_.TranslateGpuToHost(pipe_src_addr[p])) return;
            }
            if (pipe_dst_addr[p] == 0u || !mem_.TranslateGpuToHostWrite(pipe_dst_addr[p])) return;
        }

        const uint32_t scale_x = (clear_mode == 0u && downsample_x) ? 2u : 1u;
        const uint32_t scale_y = (clear_mode == 0u && downsample_y) ? 2u : 1u;
        const uint32_t output_w = window_w / scale_x;
        const uint32_t output_h = window_h / scale_y;
        if (output_w == 0u || output_h == 0u) return;

        uint32_t source_full_height = 0u;
        for (uint32_t p = 0; p < pipe_count; ++p) {
            const uint32_t bottom = pipe_offset_y[p] + window_h;
            if (bottom > source_full_height) source_full_height = bottom;
        }

        const uint32_t ts_mem_config = mem_.StateReg(kTsMemConfig);
        const bool ts_color_fast_clear = mem_.SupportsFastClear() && (ts_mem_config & (1u << 1)) != 0u;
        const bool ts_color_compression = mem_.SupportsColorCompression() && (ts_mem_config & (1u << 7)) != 0u;
        const uint32_t ts_color_status = mem_.StateReg(kTsColorStatus);
        const uint32_t ts_color_surface = mem_.StateReg(kTsColorSurface);
        const uint32_t ts_color_clear = mem_.StateReg(kTsColorClear);
        const uint32_t ts_bits_per_tile = mem_.TileStatusBitsPerTile();

        auto read_source_surface = [&](uint32_t base, uint32_t x, uint32_t y, uint32_t& argb) -> bool {
            const bool color_ts_targets_surface =
                (ts_color_fast_clear || ts_color_compression) && ts_color_surface != 0u && base == ts_color_surface;
            if (color_ts_targets_surface) {
                if (ts_bits_per_tile == 0u || ts_color_status == 0u) return false;
                const size_t byte_offset =
                    SurfaceOffset(src_stride, x, y, src_bpp, src_tiled, src_supertiled, src_supertiled_new);
                /* Etnaviv allocates TS as surface_bytes * bits_per_tile / 128:
                   one status entry therefore represents 16 surface bytes. */
                const size_t tile_index = byte_offset / 16u;
                const size_t bit_index = tile_index * ts_bits_per_tile;
                uint32_t status_byte = 0u;
                if (!ReadPackedGpu(ts_color_status, bit_index >> 3, 1u, 0u, status_byte, MmuClient::Rasterizer)) {
                    return false;
                }
                const uint32_t bit_in_byte = static_cast<uint32_t>(bit_index & 7u);
                const uint32_t state_mask = (1u << ts_bits_per_tile) - 1u;
                const uint32_t tile_state = (status_byte >> bit_in_byte) & state_mask;
                if (tile_state == 1u && ts_color_fast_clear) {
                    argb = UnpackSurfaceColor(ts_color_clear, src_fmt, 0u);
                    return true;
                }
                /* Filled, uncompressed tiles are stored normally.  Other TS
                   encodings require the compression decoder; never interpret
                   compressed bytes as ordinary pixels. */
                if (ts_color_compression || tile_state > 1u) return false;
            }
            return ReadSurfacePixelGpu(base, src_stride, x, y, src_fmt, 0u, src_tiled, src_supertiled, argb,
                                       src_supertiled_new, 0u, MmuClient::Rasterizer);
        };

        auto read_source_global = [&](uint32_t gx, uint32_t gy, uint32_t& argb) -> bool {
            if (!src_multi) {
                return read_source_surface(pipe_src_addr[0], gx, gy, argb);
            }
            for (uint32_t p = 0; p < pipe_count; ++p) {
                if (gx < pipe_offset_x[p] || gy < pipe_offset_y[p] || gx >= pipe_offset_x[p] + window_w ||
                    gy >= pipe_offset_y[p] + window_h) {
                    continue;
                }
                return read_source_surface(pipe_src_addr[p], gx - pipe_offset_x[p], gy - pipe_offset_y[p], argb);
            }
            return false;
        };

        auto swap_red_blue = [](uint32_t argb) -> uint32_t {
            return (argb & 0xFF00FF00u) | ((argb & 0x000000FFu) << 16) | ((argb & 0x00FF0000u) >> 16);
        };

        auto merge_clear_channels = [](uint32_t old_argb, uint32_t fill_argb, uint32_t nibble) -> uint32_t {
            /* Vivante clear channel order follows packed ARGB component order:
               bit0=B, bit1=G, bit2=R, bit3=A. */
            uint32_t mask = 0u;
            if (nibble & 0x1u) mask |= 0x000000FFu;
            if (nibble & 0x2u) mask |= 0x0000FF00u;
            if (nibble & 0x4u) mask |= 0x00FF0000u;
            if (nibble & 0x8u) mask |= 0xFF000000u;
            return (old_argb & ~mask) | (fill_argb & mask);
        };

        for (uint32_t p = 0; p < pipe_count; ++p) {
            const uint32_t dst_origin_x = pipe_offset_x[p] / scale_x;
            const uint32_t dst_origin_y = pipe_offset_y[p] / scale_y;
            for (uint32_t dy = 0; dy < output_h; ++dy) {
                for (uint32_t dx = 0; dx < output_w; ++dx) {
                    const uint32_t global_dx = dst_origin_x + dx;
                    const uint32_t global_dy = dst_origin_y + dy;
                    const uint32_t write_x = dst_multi ? dx : global_dx;
                    const uint32_t write_y = dst_multi ? dy : global_dy;
                    uint32_t out_argb = 0u;

                    if (clear_mode != 0u) {
                        const uint32_t group = clear_mode == 1u ? 0u : ((global_dx >> 2) & 3u);
                        const uint32_t nibble = (clear_bits >> (group * 4u)) & 0xFu;
                        if (nibble == 0u) continue;
                        out_argb = UnpackSurfaceColor(mem_.StateReg(kRsFillValue0 + group * 4u), src_fmt);
                        if (nibble != 0xFu) {
                            uint32_t old_argb = 0u;
                            if (!ReadSurfacePixelGpu(pipe_dst_addr[p], dst_stride, write_x, write_y, dst_fmt, 0u,
                                                     dst_tiled, dst_supertiled, old_argb, dst_supertiled_new, 0u,
                                                     MmuClient::PixelEngine)) {
                                continue;
                            }
                            out_argb = merge_clear_channels(old_argb, out_argb, nibble);
                        }
                    } else {
                        uint32_t sums[4] = {0u, 0u, 0u, 0u};
                        uint32_t samples = 0u;
                        for (uint32_t sy = 0; sy < scale_y; ++sy) {
                            for (uint32_t sx = 0; sx < scale_x; ++sx) {
                                const uint32_t global_sx = pipe_offset_x[p] + dx * scale_x + sx;
                                uint32_t global_sy = pipe_offset_y[p] + dy * scale_y + sy;
                                if (flip_y) global_sy = source_full_height - 1u - global_sy;
                                uint32_t sample = 0u;
                                if (!read_source_global(global_sx, global_sy, sample)) {
                                    continue;
                                }
                                sums[0] += (sample >> 24) & 0xFFu;
                                sums[1] += (sample >> 16) & 0xFFu;
                                sums[2] += (sample >> 8) & 0xFFu;
                                sums[3] += sample & 0xFFu;
                                ++samples;
                            }
                        }
                        if (samples == 0u) continue;
                        out_argb = (((sums[0] + samples / 2u) / samples) << 24) |
                                   (((sums[1] + samples / 2u) / samples) << 16) |
                                   (((sums[2] + samples / 2u) / samples) << 8) | ((sums[3] + samples / 2u) / samples);
                        if (swap_rb) out_argb = swap_red_blue(out_argb);
                    }

                    WriteSurfacePixelGpu(pipe_dst_addr[p], dst_stride, write_x, write_y, dst_fmt, 0u, dst_tiled,
                                         dst_supertiled, out_argb, dst_supertiled_new);
                }
            }
        }
    }

private:
    VivanteState& s_;
};

} // namespace imx6_vivante
