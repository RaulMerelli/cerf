#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerf::zlib_inflate {

/* Decompresses a zlib stream (RFC 1950 header + RFC 1951 DEFLATE data).
   `out_size` is the expected result size, which the caller takes from the
   container's own size attribute.  Returns an empty vector when the stream is
   malformed or does not produce exactly `out_size` bytes. */
std::vector<uint8_t> Decompress(const uint8_t* src, size_t src_size, size_t out_size, size_t* consumed = nullptr);

/* Same, for a stream whose container does not declare the result size: the
   DEFLATE data terminates itself on its final block (RFC 1951 section 3.2.3).
   `max_out` bounds the result so a malformed stream cannot exhaust memory. */
std::vector<uint8_t> DecompressGrowing(const uint8_t* src, size_t src_size, size_t max_out);

} // namespace cerf::zlib_inflate
