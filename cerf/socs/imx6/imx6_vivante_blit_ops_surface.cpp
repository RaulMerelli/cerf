#include "imx6_vivante_blit_ops.h"

namespace imx6_vivante {

bool VivanteBlitCoordinateOps::ResolveSourceCoordinate(uint32_t origin_field, bool relative, uint32_t destination_coordinate,
                                             uint32_t rectangle_offset, uint32_t& source_coordinate) {
    /* hmi_ktp400_mobile_v13 libGALCore.dll treats RELATIVE SRC_ORIGIN as signed
       16-bit offsets and ABSOLUTE SRC_ORIGIN as unsigned coordinates. */
    const int64_t coordinate =
        relative ? static_cast<int64_t>(destination_coordinate) + static_cast<int16_t>(origin_field & 0xFFFFu)
                 : static_cast<int64_t>(origin_field & 0xFFFFu) + rectangle_offset;
    if (coordinate < 0 || coordinate > UINT32_MAX) return false;
    source_coordinate = static_cast<uint32_t>(coordinate);
    return true;
}

bool VivanteBlitCoordinateOps::ResolveMonoStreamCoordinate(uint32_t origin_field, bool relative, uint32_t destination_coordinate,
                                                 uint32_t rectangle_offset, uint32_t& stream_coordinate) {
    if (relative) {
        (void)origin_field;
        (void)destination_coordinate;
        stream_coordinate = rectangle_offset;
        return true;
    }
    return ResolveSourceCoordinate(origin_field, false, destination_coordinate, rectangle_offset, stream_coordinate);
}

bool VivanteBlitCoordinateOps::ResolveMonoStreamExtent(uint32_t origin_field, bool relative, uint32_t rectangle_extent,
                                             uint32_t& stream_extent) {
    const uint64_t extent = relative ? static_cast<uint64_t>(rectangle_extent)
                                     : static_cast<uint64_t>(origin_field & 0xFFFFu) + rectangle_extent;
    if (extent == 0u || extent > UINT32_MAX) return false;
    stream_extent = static_cast<uint32_t>(extent);
    return true;
}

uint32_t VivanteBlitCoordinateOps::StretchCoordinate(uint32_t destination_offset, uint32_t source_extent,
                                           uint32_t destination_extent, uint32_t programmed_factor, bool gdi_stretch) {
    if (source_extent != 0u && destination_extent != 0u) {
        uint64_t coordinate = 0u;
        if (gdi_stretch || source_extent <= 1u || destination_extent <= 1u) {
            coordinate = (static_cast<uint64_t>(destination_offset) * source_extent) / destination_extent;
        } else {
            coordinate = (static_cast<uint64_t>(destination_offset) * (source_extent - 1u)) / (destination_extent - 1u);
        }
        if (coordinate >= source_extent) coordinate = source_extent - 1u;
        return static_cast<uint32_t>(coordinate);
    }

    if (programmed_factor != 0u)
        return static_cast<uint32_t>((static_cast<uint64_t>(destination_offset) * (programmed_factor & 0x7FFFFFFFu)) >>
                                     16);
    return destination_offset;
}

uint32_t VivanteBlitSurfaceOps::SurfaceWidthFromStride(uint32_t stride, uint32_t bpp, SurfaceLayout layout) {
    if (stride == 0u || bpp == 0u) return 0u;
    if (layout == SurfaceLayout::Linear) return stride / bpp;
    if (layout == SurfaceLayout::MinorTiled) return stride / (2u * bpp);
    return stride / (4u * bpp);
}

VivanteBlitSurfaceOps::SurfaceLayout VivanteBlitSurfaceOps::DecodeSurfaceLayout(bool tiled, bool multi_tiled, bool supertiled,
                                                                  bool minor_tiled) {
    if (minor_tiled) return SurfaceLayout::MinorTiled;
    if (multi_tiled) return supertiled ? SurfaceLayout::MultiSuperTiled : SurfaceLayout::MultiTiled;
    if (supertiled) return SurfaceLayout::SuperTiled;
    return tiled ? SurfaceLayout::Tiled : SurfaceLayout::Linear;
}

VivanteBlitSurfaceOps::SurfaceLocation VivanteBlitSurfaceOps::LocateSurface(uint32_t stride, uint32_t x, uint32_t y, uint32_t bpp,
                                                              SurfaceLayout layout, bool supertiled_new) {
    if (layout == SurfaceLayout::Linear) {
        return {0u, static_cast<size_t>(y) * stride + static_cast<size_t>(x) * bpp};
    }

    if (layout == SurfaceLayout::MinorTiled) {
        const uint32_t tile_bytes = 4u * bpp;
        const uint32_t tx = x >> 1;
        const uint32_t ty = y >> 1;
        const uint32_t in_tile = (((y & 1u) << 1) | (x & 1u)) * bpp;
        return {0u, static_cast<size_t>(ty) * stride + static_cast<size_t>(tx) * tile_bytes + in_tile};
    }

    const uint32_t tile_bytes = 16u * bpp;
    const uint32_t tx = x >> 2;
    const uint32_t ty = y >> 2;
    const uint32_t in_tile = (((y & 3u) << 2) | (x & 3u)) * bpp;

    if (layout == SurfaceLayout::Tiled) {
        return {0u, static_cast<size_t>(ty) * stride + static_cast<size_t>(tx) * tile_bytes + in_tile};
    }

    if (layout == SurfaceLayout::MultiTiled) {
        const uint32_t plane = (tx ^ ty) & 1u;
        const uint32_t local_tx = (tx & ~1u) | (ty & 1u);
        const uint32_t local_ty = ty >> 1;
        return {plane, static_cast<size_t>(local_ty) * stride + static_cast<size_t>(local_tx) * tile_bytes + in_tile};
    }

    if (layout == SurfaceLayout::MultiSuperTiled) {
        const uint32_t mx = tx >> 4;
        const uint32_t my = ty >> 5;
        const uint32_t lx = tx & 15u;
        const uint32_t ly = ty & 31u;
        const uint32_t plane = ((lx >> 1) ^ ly) & 1u;
        const uint32_t local_tile = ((ly & 24u) << 3) + ((lx >> 2) << 4) + ((ly & 1u) << 3) + (ly & 6u) + (lx & 1u);
        return {plane, static_cast<size_t>(my) * 16u * stride + static_cast<size_t>(mx) * 256u * tile_bytes +
                           static_cast<size_t>(local_tile) * tile_bytes + in_tile};
    }

    const uint32_t sx = tx >> 4;
    const uint32_t sy = ty >> 4;
    const uint32_t lx = tx & 15u;
    const uint32_t ly = ty & 15u;
    const uint32_t local_tile = supertiled_new
                                    ? (((lx & 1u) << 0) | ((ly & 1u) << 1) | ((lx & 2u) << 1) | ((ly & 2u) << 2) |
                                       ((lx & 4u) << 2) | ((ly & 4u) << 3) | ((lx & 8u) << 3) | ((ly & 8u) << 4))
                                    : (((ly >> 2) * 64u) + ((lx >> 1) * 8u) + ((ly & 3u) * 2u) + (lx & 1u));
    return {0u, static_cast<size_t>(sy) * 16u * stride + static_cast<size_t>(sx) * 256u * tile_bytes +
                    static_cast<size_t>(local_tile) * tile_bytes + in_tile};
}

VivanteBlitSurfaceOps::DeCoord VivanteBlitSurfaceOps::TransformDeCoord(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                                         uint32_t rot, uint32_t mirror) {
    if (width == 0u || height == 0u) return {x, y};

    rot = ValidDeRot(rot);
    mirror &= 3u;

    uint32_t tx = x;
    uint32_t ty = y;
    switch (rot) {
    case 1:  tx = (width > 1u && x < width) ? (width - 1u - x) : x; break;
    case 2:  ty = (height > 1u && y < height) ? (height - 1u - y) : y; break;
    case 4:
        tx = (height > 1u && y < height) ? (height - 1u - y) : y;
        ty = x;
        break;
    case 5:
        tx = (width > 1u && x < width) ? (width - 1u - x) : x;
        ty = (height > 1u && y < height) ? (height - 1u - y) : y;
        break;
    case 6:
        tx = y;
        ty = (width > 1u && x < width) ? (width - 1u - x) : x;
        break;
    default: break;
    }

    const uint32_t rw = (rot == 4u || rot == 6u) ? height : width;
    const uint32_t rh = (rot == 4u || rot == 6u) ? width : height;
    if ((mirror & 1u) && rw > 1u && tx < rw) tx = rw - 1u - tx;
    if ((mirror & 2u) && rh > 1u && ty < rh) ty = rh - 1u - ty;
    return {tx, ty};
}

} // namespace imx6_vivante
