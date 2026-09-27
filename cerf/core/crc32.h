#pragma once

#include <cstddef>
#include <cstdint>

namespace cerf {

inline uint32_t Crc32Update(uint32_t crc, const uint8_t* data, std::size_t size) {
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

inline uint32_t Crc32(const uint8_t* data, std::size_t size) {
    return Crc32Update(0u, data, size);
}

/* Crc32 is the zlib CRC-32, which ends in the final inversion an FCS carries. A hardware
   hash index is taken from the accumulator before that inversion. */
inline uint32_t Crc32Accumulator(const uint8_t* data, std::size_t size) {
    return ~Crc32(data, size);
}

}
