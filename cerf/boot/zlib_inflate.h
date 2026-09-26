#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerf::zlib_inflate {

/* RFC 1950 and RFC 1951 define the zlib wrapper and DEFLATE stream. */
std::vector<uint8_t> Decompress(const uint8_t* src, size_t src_size, size_t out_size, size_t* consumed = nullptr);

/* RFC 1951 §3.2.3. */
std::vector<uint8_t> DecompressGrowing(const uint8_t* src, size_t src_size, size_t max_out);

}
