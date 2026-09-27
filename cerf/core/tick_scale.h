#pragma once

#include <cstdint>

inline uint64_t ScaleU64(uint64_t value, uint64_t num, uint64_t den) {
    return (value / den) * num + ((value % den) * num) / den;
}

inline uint64_t ScaleU64Ceil(uint64_t value, uint64_t num, uint64_t den) {
    return (value / den) * num + ((value % den) * num + den - 1u) / den;
}
