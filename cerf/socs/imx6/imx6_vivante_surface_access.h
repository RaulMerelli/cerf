#pragma once

#include <cstddef>
#include <cstdint>

#include "imx6_vivante_blit_ops.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"

namespace imx6_vivante {

class VivanteSurfaceAccess : protected VivanteBlitOps {
protected:
    explicit VivanteSurfaceAccess(VivanteMem& mem) : mem_(mem) {}

    uint32_t RequireMemoryDeFormat(uint32_t fmt, const char* operation) const {
        const uint32_t bpp = BppFromDeFormat(fmt);
        if (!IsKnownDeFormat(fmt) || bpp == 0u) {
            mem_.HaltUnsupported(operation, fmt & 0x1Fu, 0u);
        }
        return bpp;
    }

    static bool AddGpuOffset(uint32_t base, size_t offset, uint32_t& address) {
        if (offset > 0xFFFFFFFFull || static_cast<uint64_t>(base) + static_cast<uint64_t>(offset) > 0xFFFFFFFFull) {
            return false;
        }
        address = base + static_cast<uint32_t>(offset);
        return true;
    }

    bool ReadPackedGpu(uint32_t base, size_t offset, uint32_t byte_count, uint32_t endian, uint32_t& packed,
                       MmuClient client = MmuClient::Texture) const {
        packed = 0u;
        if (base == 0u || byte_count == 0u || byte_count > 4u) return false;
        size_t mapped[4]{};
        size_t first = static_cast<size_t>(-1);
        size_t last = 0u;
        for (uint32_t byte = 0u; byte < byte_count; ++byte) {
            mapped[byte] = EndianByteOffset(offset + byte, endian);
            if (mapped[byte] < first) first = mapped[byte];
            if (mapped[byte] > last) last = mapped[byte];
        }
        const size_t span = last - first + 1u;
        if (span > 4u) return false;
        uint32_t address = 0u;
        if (!AddGpuOffset(base, first, address)) return false;
        uint8_t bytes[4]{};
        if (!mem_.ReadGpuBytes(address, bytes, span, client)) return false;
        for (uint32_t byte = 0u; byte < byte_count; ++byte) {
            packed |= static_cast<uint32_t>(bytes[mapped[byte] - first]) << (byte * 8u);
        }
        return true;
    }

    bool WritePackedGpu(uint32_t base, size_t offset, uint32_t byte_count, uint32_t endian, uint32_t packed,
                        MmuClient client = MmuClient::PixelEngine) const {
        if (base == 0u || byte_count == 0u || byte_count > 4u) return false;
        size_t mapped[4]{};
        size_t first = static_cast<size_t>(-1);
        size_t last = 0u;
        for (uint32_t byte = 0u; byte < byte_count; ++byte) {
            mapped[byte] = EndianByteOffset(offset + byte, endian);
            if (mapped[byte] < first) first = mapped[byte];
            if (mapped[byte] > last) last = mapped[byte];
        }
        const size_t span = last - first + 1u;
        if (span > 4u) return false;
        uint32_t address = 0u;
        if (!AddGpuOffset(base, first, address)) return false;
        uint8_t bytes[4]{};
        if (!mem_.ReadGpuBytes(address, bytes, span, client)) return false;
        for (uint32_t byte = 0u; byte < byte_count; ++byte) {
            bytes[mapped[byte] - first] = static_cast<uint8_t>(packed >> (byte * 8u));
        }
        return mem_.WriteGpuBytes(address, bytes, span, client);
    }

    bool ReadLayoutPackedGpu(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                             uint32_t byte_count, SurfaceLayout layout, uint32_t endian, uint32_t& packed,
                             bool supertiled_new = false, MmuClient client = MmuClient::Texture) const {
        if (base == 0u || byte_count == 0u || byte_count > 4u) return false;
        const SurfaceLocation location = LocateSurface(stride, x, y, byte_count, layout, supertiled_new);
        const uint32_t selected_base = location.plane ? extra_base : base;
        if (selected_base == 0u) return false;
        return ReadPackedGpu(selected_base, location.offset, byte_count, endian, packed, client);
    }

    bool WriteLayoutPackedGpu(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                              uint32_t byte_count, SurfaceLayout layout, uint32_t endian, uint32_t packed,
                              bool supertiled_new = false, MmuClient client = MmuClient::PixelEngine) const {
        if (base == 0u || byte_count == 0u || byte_count > 4u) return false;
        const SurfaceLocation location = LocateSurface(stride, x, y, byte_count, layout, supertiled_new);
        const uint32_t selected_base = location.plane ? extra_base : base;
        if (selected_base == 0u) return false;
        return WritePackedGpu(selected_base, location.offset, byte_count, endian, packed, client);
    }

    bool ReadSurfacePackedGpuLayout(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                                    uint32_t fmt, SurfaceLayout layout, uint32_t& packed, bool supertiled_new = false,
                                    uint32_t endian = 0u, MmuClient client = MmuClient::Texture) const {
        const uint32_t bpp = RequireMemoryDeFormat(fmt, "imx6-vivante unsupported surface-read format");
        return ReadLayoutPackedGpu(base, extra_base, stride, x, y, bpp, layout, endian, packed, supertiled_new, client);
    }

    bool ReadSurfacePixelGpuLayout(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                                   uint32_t fmt, uint32_t swizzle, SurfaceLayout layout, uint32_t& argb,
                                   bool supertiled_new = false, uint32_t endian = 0u,
                                   MmuClient client = MmuClient::Texture) const {
        uint32_t packed = 0u;
        if (!ReadSurfacePackedGpuLayout(base, extra_base, stride, x, y, fmt, layout, packed, supertiled_new, endian,
                                        client)) {
            return false;
        }
        argb = UnpackSurfaceColor(packed, fmt, swizzle);
        return true;
    }

    bool WriteSurfacePixelGpuLayout(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                                    uint32_t fmt, uint32_t swizzle, SurfaceLayout layout, uint32_t argb,
                                    bool supertiled_new = false, uint32_t endian = 0u,
                                    MmuClient client = MmuClient::PixelEngine) const {
        const uint32_t bpp = RequireMemoryDeFormat(fmt, "imx6-vivante unsupported surface-write format");
        const uint32_t packed = PackSurfaceColor(argb, fmt, swizzle);
        return WriteLayoutPackedGpu(base, extra_base, stride, x, y, bpp, layout, endian, packed, supertiled_new,
                                    client);
    }

    bool ReadSurfacePackedGpu(uint32_t base, uint32_t stride, uint32_t x, uint32_t y, uint32_t fmt, bool tiled,
                              bool supertiled, uint32_t& packed, bool supertiled_new = false, uint32_t endian = 0u,
                              MmuClient client = MmuClient::Texture) const {
        const uint32_t bpp = RequireMemoryDeFormat(fmt, "imx6-vivante unsupported packed-read format");
        if (base == 0u) return false;
        const size_t offset = SurfaceOffset(stride, x, y, bpp, tiled, supertiled, supertiled_new);
        return ReadPackedGpu(base, offset, bpp, endian, packed, client);
    }

    bool ReadSurfacePixelGpu(uint32_t base, uint32_t stride, uint32_t x, uint32_t y, uint32_t fmt, uint32_t swizzle,
                             bool tiled, bool supertiled, uint32_t& argb, bool supertiled_new = false,
                             uint32_t endian = 0u, MmuClient client = MmuClient::Texture) const {
        uint32_t packed = 0u;
        if (!ReadSurfacePackedGpu(base, stride, x, y, fmt, tiled, supertiled, packed, supertiled_new, endian, client)) {
            return false;
        }
        argb = UnpackSurfaceColor(packed, fmt, swizzle);
        return true;
    }

    bool WriteSurfacePixelGpu(uint32_t base, uint32_t stride, uint32_t x, uint32_t y, uint32_t fmt, uint32_t swizzle,
                              bool tiled, bool supertiled, uint32_t argb, bool supertiled_new = false,
                              uint32_t endian = 0u, MmuClient client = MmuClient::PixelEngine) const {
        const uint32_t bpp = RequireMemoryDeFormat(fmt, "imx6-vivante unsupported pixel-write format");
        if (base == 0u) return false;
        const uint32_t packed = PackSurfaceColor(argb, fmt, swizzle);
        const size_t offset = SurfaceOffset(stride, x, y, bpp, tiled, supertiled, supertiled_new);
        return WritePackedGpu(base, offset, bpp, endian, packed, client);
    }

    bool ApplyPe10ClearGpu(uint32_t base, size_t offset, uint32_t pixel_bytes, uint32_t low, uint32_t high,
                           uint32_t byte_mask, uint32_t endian) const {
        if (base == 0u || pixel_bytes == 0u) return false;
        const uint64_t pattern = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
        bool wrote = false;
        for (uint32_t byte = 0u; byte < pixel_bytes; ++byte) {
            const uint32_t slot = static_cast<uint32_t>((offset + byte) & 7u);
            if ((byte_mask & (1u << slot)) == 0u) continue;
            const size_t mapped = EndianByteOffset(offset + byte, endian);
            uint32_t address = 0u;
            if (!AddGpuOffset(base, mapped, address)) return false;
            const uint8_t value = static_cast<uint8_t>(pattern >> (slot * 8u));
            if (!mem_.WriteGpuBytes(address, &value, 1u, MmuClient::PixelEngine)) {
                return false;
            }
            wrote = true;
        }
        return wrote || byte_mask == 0u;
    }

    bool ReadVrYuvPixelGpu(uint32_t y_address, uint32_t y_extra_address, uint32_t y_stride, uint32_t u_address,
                           uint32_t u_stride, uint32_t v_address, uint32_t v_stride, uint32_t x, uint32_t y,
                           uint32_t fmt, SurfaceLayout layout, uint32_t endian, bool uv_swizzle, bool bt709,
                           uint32_t& argb) const {
        if (y_address == 0u || y_stride == 0u) return false;

        auto read_plane = [&](uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t px, uint32_t py,
                              uint32_t bytes, SurfaceLayout plane_layout, uint32_t& value) -> bool {
            return ReadLayoutPackedGpu(base, extra_base, stride, px, py, bytes, plane_layout, endian, value, false,
                                       MmuClient::Texture);
        };

        uint32_t yy = 0u;
        uint32_t uu = 128u;
        uint32_t vv = 128u;
        switch (fmt & 0x1Fu) {
        case 7u: { /* YUY2: [Y0 U] [Y1 V]. */
            uint32_t even = 0u;
            uint32_t odd = 0u;
            const uint32_t pair_x = x & ~1u;
            if (!read_plane(y_address, y_extra_address, y_stride, pair_x, y, 2u, layout, even) ||
                !read_plane(y_address, y_extra_address, y_stride, pair_x + 1u, y, 2u, layout, odd)) {
                return false;
            }
            yy = (x & 1u) ? (odd & 0xFFu) : (even & 0xFFu);
            uu = (even >> 8) & 0xFFu;
            vv = (odd >> 8) & 0xFFu;
            break;
        }
        case 8u: { /* UYVY: [U Y0] [V Y1]. */
            uint32_t even = 0u;
            uint32_t odd = 0u;
            const uint32_t pair_x = x & ~1u;
            if (!read_plane(y_address, y_extra_address, y_stride, pair_x, y, 2u, layout, even) ||
                !read_plane(y_address, y_extra_address, y_stride, pair_x + 1u, y, 2u, layout, odd)) {
                return false;
            }
            yy = (x & 1u) ? ((odd >> 8) & 0xFFu) : ((even >> 8) & 0xFFu);
            uu = even & 0xFFu;
            vv = odd & 0xFFu;
            break;
        }
        case 15u: { /* YV12/YUV420: independent 8-bit chroma planes. */
            if (u_address == 0u || v_address == 0u || u_stride == 0u || v_stride == 0u) {
                return false;
            }
            if (!read_plane(y_address, 0u, y_stride, x, y, 1u, layout, yy) ||
                !read_plane(u_address, 0u, u_stride, x >> 1, y >> 1, 1u, layout, uu) ||
                !read_plane(v_address, 0u, v_stride, x >> 1, y >> 1, 1u, layout, vv)) {
                return false;
            }
            break;
        }
        case 17u:   /* NV12: UV plane is 2-byte 4:2:0 samples. */
        case 18u: { /* NV16: UV plane is 2-byte 4:2:2 samples. */
            if (u_address == 0u || u_stride == 0u) return false;
            const uint32_t chroma_y = ((fmt & 0x1Fu) == 17u) ? (y >> 1) : y;
            uint32_t uv = 0u;
            if (!read_plane(y_address, 0u, y_stride, x, y, 1u, layout, yy) ||
                !read_plane(u_address, 0u, u_stride, x >> 1, chroma_y, 2u, layout, uv)) {
                return false;
            }
            uu = uv & 0xFFu;
            vv = (uv >> 8) & 0xFFu;
            break;
        }
        default: return false;
        }
        if (uv_swizzle) {
            const uint32_t tmp = uu;
            uu = vv;
            vv = tmp;
        }
        argb = YuvToArgb(yy, uu, vv, bt709);
        return true;
    }

    VivanteMem& mem_;
};

} // namespace imx6_vivante
