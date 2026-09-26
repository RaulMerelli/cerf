#pragma once

#include <cstdint>

constexpr uint32_t MB(uint32_t mb) {
    return mb * 0x100000u;
}

constexpr uint32_t kDramVa = 0x80000000u;
constexpr uint32_t kDramPa = 0x10000000u;
constexpr uint32_t kDramSize = MB(384);
/* Micron FBGA D9QTF = MT41K128M16JT-125 XIT:K (2 Gb DDR3L); mainboard
   A5E31667216-AE carries two per side, two 16-bit parts per chip select. */
constexpr uint32_t kDdrChipSelectSize = MB(512);
constexpr uint32_t kDdrSize = 2u * kDdrChipSelectSize;

constexpr uint32_t kInitStackTopPa = kDramPa + kDramSize;
