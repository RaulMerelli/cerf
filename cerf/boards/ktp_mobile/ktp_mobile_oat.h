#pragma once

#include <cstdint>

constexpr uint32_t MB(uint32_t mb) {
    return mb * 0x100000u;
}

constexpr uint32_t kDramVa = 0x80000000u;
constexpr uint32_t kDramPa = 0x10000000u;
constexpr uint32_t kDramSize = MB(384);

constexpr uint32_t kInitStackTopPa = kDramPa + kDramSize;
