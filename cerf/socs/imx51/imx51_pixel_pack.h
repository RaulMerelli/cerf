#pragma once

#include <cstdint>

/* Shared i.MX51 GPU pixel packing. Both the z160 GPU2D rasterizer and the z430
   3D blit convert an 8888 source into a 565 destination surface. */
namespace imx51_pixel {

/* ARGB8888 -> RGB565 (truncate low bits; the exact inverse of the 5:6:5
   bit-replicate expand, so expand(pack(x)) round-trips - which is what the IPU
   scanout's Expand565 does when it reads the same buffer). R[7:3]->[15:11],
   G[7:2]->[10:5], B[7:3]->[4:0]. */
inline uint16_t PackArgb565(uint32_t argb) {
    return static_cast<uint16_t>((((argb >> 19) & 0x1Fu) << 11)
                               | (((argb >> 10) & 0x3Fu) << 5)
                               | ((argb >> 3) & 0x1Fu));
}

}  // namespace imx51_pixel
